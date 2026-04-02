#include "tabs.h"

#include <stdio.h>
#include <string.h>

#ifndef _WIN32
#include "pty_unix.h"
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

enum {
    TAB_TOGGLE_W = 22,
    TAB_TOGGLE_H = 28,
    TAB_OSC_FILTER_MODE_NORMAL = 0,
    TAB_OSC_FILTER_MODE_ESC = 1,
    TAB_OSC_FILTER_MODE_OSC = 2,
};

typedef struct {
    Tab *tab;
} TabPtySinkCtx;

static void tab_agent_state_change_noop(void *userdata,
                                        const Tab *tab,
                                        const GhostlingAgentState *before,
                                        const GhostlingAgentState *after)
{
    (void)userdata;
    (void)tab;
    (void)before;
    (void)after;
}

static void tab_ingest_clipboard(Tab *t, const uint8_t *data, size_t len)
{
    osc52_clipboard_scan(&t->osc52, data, len);
}

static void tab_notify_agent_state_if_changed(Tab *t,
                                              const GhostlingAgentState *before)
{
    if (!t->agent_state_hook)
        return;

    const GhostlingAgentState *after = &t->agent_state;
    if (before->value == after->value && before->source == after->source &&
        before->confidence == after->confidence &&
        strcmp(before->protocol_agent, after->protocol_agent) == 0)
        return;

    t->agent_state_hook(t->agent_state_hook_userdata, t, before, after);
}

static void tab_ingest_agent(Tab *t, const uint8_t *data, size_t len)
{
    GhostlingAgentState before = t->agent_state;
    ghostling_agent_state_feed_output(&t->agent_state, data, len);
    tab_notify_agent_state_if_changed(t, &before);
}

static void tab_ingest_effects(Tab *t, const uint8_t *data, size_t len)
{
    effect_scan_icon_osc(&t->effects, data, len);
}

static void tab_ingest_terminal(Tab *t, const uint8_t *data, size_t len);

static void tab_osc1_filter_reset(Tab *t)
{
    t->osc1_filter_esc_pending = false;
    t->osc1_filter_cmd_decided = false;
    t->osc1_filter_drop = false;
    t->osc1_filter_cmd = 0;
    t->osc1_filter_cmd_digits = 0;
    t->osc1_filter_raw_len = 0;
}

static void tab_osc1_filter_append_raw(Tab *t, uint8_t b)
{
    if (t->osc1_filter_raw_len >= sizeof(t->osc1_filter_raw)) {
        t->osc1_filter_drop = true;
        return;
    }
    t->osc1_filter_raw[t->osc1_filter_raw_len++] = b;
}

static void tab_osc1_filter_forward_raw(Tab *t)
{
    if (t->osc1_filter_raw_len == 0)
        return;
    tab_ingest_terminal(t, t->osc1_filter_raw, t->osc1_filter_raw_len);
}

static void tab_ingest_terminal_filtered(Tab *t, const uint8_t *data, size_t len)
{
    if (!t->terminal || !data || len == 0)
        return;

    uint8_t out[4096];
    size_t out_len = 0;

#define TAB_FLUSH_OUT()                                                         \
    do {                                                                         \
        if (out_len > 0) {                                                       \
            tab_ingest_terminal(t, out, out_len);                                \
            out_len = 0;                                                         \
        }                                                                        \
    } while (0)

    for (size_t i = 0; i < len; i++) {
        uint8_t b = data[i];

        switch (t->osc1_filter_mode) {
        case TAB_OSC_FILTER_MODE_NORMAL:
            if (b == 0x1B) {
                TAB_FLUSH_OUT();
                t->osc1_filter_mode = TAB_OSC_FILTER_MODE_ESC;
                tab_osc1_filter_reset(t);
                tab_osc1_filter_append_raw(t, b);
            } else {
                if (out_len < sizeof(out))
                    out[out_len++] = b;
            }
            break;

        case TAB_OSC_FILTER_MODE_ESC:
            tab_osc1_filter_append_raw(t, b);
            if (b == ']') {
                t->osc1_filter_mode = TAB_OSC_FILTER_MODE_OSC;
            } else {
                tab_osc1_filter_forward_raw(t);
                t->osc1_filter_mode = TAB_OSC_FILTER_MODE_NORMAL;
                tab_osc1_filter_reset(t);
            }
            break;

        case TAB_OSC_FILTER_MODE_OSC:
            tab_osc1_filter_append_raw(t, b);

            if (t->osc1_filter_esc_pending) {
                t->osc1_filter_esc_pending = false;
                if (b == '\\') {
                    if (!t->osc1_filter_drop)
                        tab_osc1_filter_forward_raw(t);
                    t->osc1_filter_mode = TAB_OSC_FILTER_MODE_NORMAL;
                    tab_osc1_filter_reset(t);
                    break;
                }
            }

            if (!t->osc1_filter_cmd_decided) {
                if (b >= '0' && b <= '9') {
                    if (t->osc1_filter_cmd <= 99999999u)
                        t->osc1_filter_cmd =
                            t->osc1_filter_cmd * 10u + (unsigned)(b - '0');
                    t->osc1_filter_cmd_digits++;
                } else if (b == ';') {
                    t->osc1_filter_cmd_decided = true;
                    t->osc1_filter_drop =
                        (t->osc1_filter_cmd_digits > 0 &&
                         t->osc1_filter_cmd == 1u);
                } else {
                    t->osc1_filter_cmd_decided = true;
                    t->osc1_filter_drop = false;
                }
            }

            if (b == 0x07) {
                if (!t->osc1_filter_drop)
                    tab_osc1_filter_forward_raw(t);
                t->osc1_filter_mode = TAB_OSC_FILTER_MODE_NORMAL;
                tab_osc1_filter_reset(t);
            } else if (b == 0x1B) {
                t->osc1_filter_esc_pending = true;
            }
            break;

        default:
            t->osc1_filter_mode = TAB_OSC_FILTER_MODE_NORMAL;
            tab_osc1_filter_reset(t);
            break;
        }

        if (out_len == sizeof(out)) {
            TAB_FLUSH_OUT();
        }
    }

    TAB_FLUSH_OUT();

#undef TAB_FLUSH_OUT
}

static void tab_ingest_terminal(Tab *t, const uint8_t *data, size_t len)
{
    if (!t->terminal)
        return;
    ghostty_terminal_vt_write(t->terminal, data, len);
}

static void tab_ingest_output(void *userdata, const uint8_t *data, size_t len)
{
    if (!userdata || !data || len == 0)
        return;

    TabPtySinkCtx *ctx = (TabPtySinkCtx *)userdata;
    Tab *t = ctx->tab;

    tab_ingest_clipboard(t, data, len);
    tab_ingest_agent(t, data, len);
    tab_ingest_effects(t, data, len);
    tab_ingest_terminal_filtered(t, data, len);
}

/** Fake bold for tab index digits without a separate bold font face. */
static void draw_text_synthetic_bold(Font font, const char *text, Vector2 pos,
                                     float font_size, Color fg)
{
    DrawTextEx(font, text, (Vector2){pos.x + 1.0f, pos.y}, font_size, 0, fg);
    DrawTextEx(font, text, (Vector2){pos.x, pos.y + 1.0f}, font_size, 0, fg);
    DrawTextEx(font, text, (Vector2){pos.x + 1.0f, pos.y + 1.0f}, font_size, 0,
               fg);
    DrawTextEx(font, text, pos, font_size, 0, fg);
}

static void truncate_to_width(Font font, float font_size, const char *src,
                              float max_w, char *out, size_t outsz)
{
    if (max_w < 8.0f) {
        out[0] = '\0';
        return;
    }
    if (MeasureTextEx(font, src, font_size, 0).x <= max_w) {
        snprintf(out, outsz, "%s", src);
        return;
    }
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", src);
    size_t n = strlen(tmp);
    while (n > 0) {
        tmp[n - 1] = '\0';
        n--;
        if (MeasureTextEx(font, tmp, font_size, 0).x <= max_w) {
            snprintf(out, outsz, "%s", tmp);
            return;
        }
    }
    if (outsz > 0)
        out[0] = '\0';
}

void tab_init_struct(Tab *t)
{
    memset(t, 0, sizeof(*t));
    ghostling_agent_state_init(&t->agent_state);
    t->agent_state_hook = tab_agent_state_change_noop;
    osc52_clipboard_init(&t->osc52);
#ifdef _WIN32
    t->pty_ctx.hpc = INVALID_HANDLE_VALUE;
    t->pty_ctx.process = INVALID_HANDLE_VALUE;
    t->pty_ctx.pipe_in = INVALID_HANDLE_VALUE;
    t->pty_ctx.pipe_out = INVALID_HANDLE_VALUE;
    InitializeCriticalSection(&t->pty_rb.cs);
    t->pty_cs_inited = true;
#endif
}

void tab_set_agent_state_hook(
    Tab *t,
    void (*hook)(void *userdata, const Tab *tab,
                 const GhostlingAgentState *before,
                 const GhostlingAgentState *after),
    void *userdata)
{
    if (!t)
        return;
    t->agent_state_hook = hook;
    t->agent_state_hook_userdata = userdata;
}

void tab_agent_state_on_local_input(Tab *t)
{
    if (!t)
        return;
    GhostlingAgentState before = t->agent_state;
    ghostling_agent_state_on_local_input(&t->agent_state);
    tab_notify_agent_state_if_changed(t, &before);
}

void tab_agent_state_on_process_exit(Tab *t, int exit_status)
{
    if (!t)
        return;
    GhostlingAgentState before = t->agent_state;
    ghostling_agent_state_on_process_exit(&t->agent_state, exit_status);
    tab_notify_agent_state_if_changed(t, &before);
}

void tab_free(Tab *t)
{
    if (t->terminal) {
        ghostty_terminal_free(t->terminal);
        t->terminal = NULL;
    }

#ifdef _WIN32
    if (t->pty_ctx.hpc != INVALID_HANDLE_VALUE) {
        ClosePseudoConsole(t->pty_ctx.hpc);
        t->pty_ctx.hpc = INVALID_HANDLE_VALUE;
    }
    if (t->pty_reader) {
        if (WaitForSingleObject(t->pty_reader, 3000) != WAIT_OBJECT_0) {
            fprintf(stderr, "ghostling: pty reader thread did not exit, terminating\n");
            TerminateThread(t->pty_reader, 1);
        }
        CloseHandle(t->pty_reader);
        t->pty_reader = NULL;
    }
    pty_cleanup_win(&t->pty_ctx);
    if (t->pty_cs_inited) {
        DeleteCriticalSection(&t->pty_rb.cs);
        t->pty_cs_inited = false;
    }
#else
    if (t->pty_fd >= 0) {
        close(t->pty_fd);
        t->pty_fd = -1;
    }
    if (t->child > 0 && !t->child_reaped) {
        if (!t->child_exited)
            kill(t->child, SIGHUP);
        waitpid(t->child, NULL, 0);
        t->child = -1;
    }
#endif

    osc52_clipboard_deinit(&t->osc52);

    memset(t, 0, sizeof(*t));
#ifdef _WIN32
    t->pty_ctx.hpc = INVALID_HANDLE_VALUE;
    t->pty_ctx.process = INVALID_HANDLE_VALUE;
    t->pty_ctx.pipe_in = INVALID_HANDLE_VALUE;
    t->pty_ctx.pipe_out = INVALID_HANDLE_VALUE;
#endif
}

void tab_bind_ghostty_callbacks(Tab *t)
{
    ghostty_terminal_set(t->terminal, GHOSTTY_TERMINAL_OPT_USERDATA, &t->effects);
    ghostty_terminal_set(t->terminal, GHOSTTY_TERMINAL_OPT_WRITE_PTY,
                         (const void *)effect_write_pty);
    ghostty_terminal_set(t->terminal, GHOSTTY_TERMINAL_OPT_SIZE,
                         (const void *)effect_size);
    ghostty_terminal_set(t->terminal, GHOSTTY_TERMINAL_OPT_DEVICE_ATTRIBUTES,
                         (const void *)effect_device_attributes);
    ghostty_terminal_set(t->terminal, GHOSTTY_TERMINAL_OPT_XTVERSION,
                         (const void *)effect_xtversion);
    ghostty_terminal_set(t->terminal, GHOSTTY_TERMINAL_OPT_TITLE_CHANGED,
                         (const void *)effect_title_changed);
    ghostty_terminal_set(t->terminal, GHOSTTY_TERMINAL_OPT_COLOR_SCHEME,
                         (const void *)effect_color_scheme);
}

void tab_display_title(const Tab *t, size_t tab_index_one_based, char *out,
                       size_t outsz)
{
    const EffectsContext *e = &t->effects;
    if (e->title_override[0] != '\0') {
        snprintf(out, outsz, "%s", e->title_override);
        return;
    }
    if (e->title_icon[0] != '\0') {
        snprintf(out, outsz, "%s", e->title_icon);
        return;
    }
    if (e->title_shell[0] != '\0') {
        snprintf(out, outsz, "%s", e->title_shell);
        return;
    }
    (void)tab_index_one_based;
    if (outsz > 0)
        out[0] = '\0';
}

bool tab_start_shell(Tab *t, uint16_t cols, uint16_t rows, int cell_width,
                     int cell_height, const char *shell_override)
{
    tab_init_struct(t);

    GhosttyTerminalOptions opts = {.cols = cols,
                                   .rows = rows,
                                   .max_scrollback = 1000};
    GhosttyResult err = ghostty_terminal_new(NULL, &t->terminal, opts);
    if (err != GHOSTTY_SUCCESS) {
        fprintf(stderr, "ghostty_terminal_new failed (%d)\n", err);
        tab_free(t);
        return false;
    }

#ifdef _WIN32
    if (!pty_spawn_win32(&t->pty_ctx, cols, rows, shell_override)) {
        tab_free(t);
        return false;
    }
    t->pty_rb.pipe = t->pty_ctx.pipe_out;
    t->pty_reader = CreateThread(NULL, 0, pty_reader_thread, &t->pty_rb, 0, NULL);
    if (!t->pty_reader) {
        win_perror("CreateThread (pty reader)");
        tab_free(t);
        return false;
    }
    t->effects.pty_fd = t->pty_ctx.pipe_in;
#else
    t->pty_fd =
        pty_spawn_unix(&t->child, cols, rows, shell_override, cell_width,
                       cell_height);
    if (t->pty_fd < 0) {
        tab_free(t);
        return false;
    }
    t->effects.pty_fd = t->pty_fd;
#endif

    t->effects.cell_width = cell_width;
    t->effects.cell_height = cell_height;
    t->effects.cols = cols;
    t->effects.rows = rows;
    t->effects.title_shell[0] = '\0';
    t->effects.title_icon[0] = '\0';
    t->effects.title_override[0] = '\0';
    t->effects.pwd[0] = '\0';

    tab_bind_ghostty_callbacks(t);

    t->child_exited = false;
    t->child_reaped = false;
    t->child_exit_status = -1;
    t->in_use = true;
    return true;
}

void tab_resize_pty(Tab *t, uint16_t cols, uint16_t rows, int cell_width,
                    int cell_height)
{
    if (!t->in_use)
        return;
    t->effects.cols = cols;
    t->effects.rows = rows;
    t->effects.cell_width = cell_width;
    t->effects.cell_height = cell_height;
    ghostty_terminal_resize(t->terminal, cols, rows, (uint32_t)cell_width,
                            (uint32_t)cell_height);
#ifdef _WIN32
    pty_resize_win32(t->pty_ctx.hpc, cols, rows);
#else
    struct winsize new_ws = {
        .ws_row = rows,
        .ws_col = cols,
        .ws_xpixel = (unsigned short)(cols * cell_width),
        .ws_ypixel = (unsigned short)(rows * cell_height),
    };
    ioctl(t->pty_fd, TIOCSWINSZ, &new_ws);
#endif
}

PtyReadResult tab_drain(Tab *t)
{
    if (!t->in_use || t->child_exited)
        return PTY_READ_OK;

    TabPtySinkCtx sink = {.tab = t};
#ifdef _WIN32
    return pty_buf_drain(&t->pty_rb, tab_ingest_output, &sink);
#else
    return pty_read_unix(t->pty_fd, tab_ingest_output, &sink);
#endif
}

PtyHandle tab_pty_write(Tab *t)
{
#ifdef _WIN32
    return t->pty_ctx.pipe_in;
#else
    return t->pty_fd;
#endif
}

bool tab_splitter_hit(Vector2 mpos, int strip_w, int scr_h)
{
    (void)scr_h;
    return mpos.x >= (float)strip_w &&
           mpos.x <= (float)(strip_w + TAB_SPLITTER_GRAB);
}

static float tab_icon_font_size(float base_font)
{
    float size = base_font * 1.3f;
    if (size < base_font + 2.0f)
        size = base_font + 2.0f;
    return size;
}

/** Shared geometry for the collapse/expand affordance (splitter, vertical center). */
static void tab_splitter_toggle_bounds(int effective_strip_w, int scr_h, int *tx,
                                     int *ty, int *tw, int *th)
{
    *tw = TAB_TOGGLE_W;
    *th = TAB_TOGGLE_H;
    /* Align right edge to splitter line at x = strip_w - 1. */
    *tx = effective_strip_w - *tw - 1;
    *ty = scr_h / 2 - *th / 2;
}

/** Collapsed: small rect near left edge, vertically centered (matches tab_splitter_toggle_draw). */
static void tab_collapsed_expand_bounds(int scr_h, int *tx, int *ty, int *tw, int *th)
{
    *tw = TAB_TOGGLE_W;
    *th = TAB_TOGGLE_H;
    *tx = (TAB_COLLAPSED_EDGE_HOVER - *tw) / 2;
    if (*tx < 0)
        *tx = 0;
    *ty = scr_h / 2 - *th / 2;
}

bool tab_splitter_toggle_hit(Vector2 mpos, int effective_strip_w, int scr_h)
{
    /* Collapsed: only the chevron rect — rest of left margin is for terminal / selection. */
    if (effective_strip_w == 0) {
        int tx, ty, tw, th;
        tab_collapsed_expand_bounds(scr_h, &tx, &ty, &tw, &th);
        return mpos.x >= (float)tx && mpos.x < (float)(tx + tw) &&
               mpos.y >= (float)ty && mpos.y < (float)(ty + th);
    }
    /* Expanded: only the chevron rect (center height). Rest of splitter = drag. */
    int tx, ty, tw, th;
    tab_splitter_toggle_bounds(effective_strip_w, scr_h, &tx, &ty, &tw, &th);
    return mpos.x >= (float)tx && mpos.x < (float)(tx + tw) &&
           mpos.y >= (float)ty && mpos.y < (float)(ty + th);
}

bool tab_strip_hit(Vector2 mpos, int strip_w, int scr_h, size_t n_tabs,
                   size_t *idx, TabStripAction *act, int tab_title_h,
                   int tab_reserved_h, bool strip_collapsed)
{
    if (strip_collapsed)
        return false;
    /* Collapse control: do not treat as tab row / new tab. */
    if (tab_splitter_toggle_hit(mpos, strip_w, scr_h))
        return false;
    if (tab_title_h < 1)
        tab_title_h = 1;
    if (tab_reserved_h < 0)
        tab_reserved_h = 0;
    int row_h = tab_title_h + tab_reserved_h;
    if (row_h < 1)
        row_h = 1;

    *act = TAB_STRIP_NONE;
    if (mpos.x < 0.0f || mpos.x >= (float)strip_w)
        return false;

    int new_y0 = scr_h - TAB_NEW_H;
    if (mpos.y >= (float)new_y0) {
        *act = TAB_STRIP_NEW;
        return true;
    }

    int row = (int)(mpos.y / (float)row_h);
    if (row < 0) {
        *act = TAB_STRIP_NONE;
        return true;
    }

    size_t max_vis = (size_t)((new_y0 > 0) ? (new_y0 / row_h) : 1);
    if (max_vis == 0)
        max_vis = 1;

    if ((size_t)row >= n_tabs || (size_t)row >= max_vis) {
        *act = TAB_STRIP_NONE;
        return true;
    }

    *idx = (size_t)row;
    int y_in_tab = (int)mpos.y - row * row_h;
    bool in_title = y_in_tab < tab_title_h;
    if (mpos.x >= (float)(strip_w - TAB_CLOSE_W) && in_title)
        *act = TAB_STRIP_CLOSE;
    else
        *act = TAB_STRIP_SELECT;
    return true;
}

void tab_strip_draw(Font font, float font_size, int strip_w, int scr_h,
                    Tab *const *tabs, size_t n_tabs, size_t active_idx,
                    size_t edit_idx, const char *edit_buf, Color strip_bg,
                    Color tab_index_bg, Color tab_reserved_bg, Color tab_bg,
                    Color tab_active, Color border, Color fg, Color edit_bg,
                    int tab_title_h, int tab_reserved_h, bool strip_collapsed)
{
    if (strip_collapsed)
        return;

    if (tab_title_h < 1)
        tab_title_h = 1;
    if (tab_reserved_h < 0)
        tab_reserved_h = 0;
    int row_h = tab_title_h + tab_reserved_h;
    if (row_h < 1)
        row_h = 1;

    int ix = TAB_INDEX_COL_W;
    if (ix >= strip_w - (int)TAB_CLOSE_W - 8)
        ix = (strip_w > 40) ? 20 : 0;

    DrawRectangle(0, 0, strip_w, scr_h, strip_bg);
    DrawRectangle(strip_w - 1, 0, 1, scr_h, border);

    int new_y0 = scr_h - TAB_NEW_H;
    size_t max_vis = (size_t)((new_y0 > 0) ? (new_y0 / row_h) : 1);
    if (max_vis == 0)
        max_vis = 1;

    float label_max_w = (float)(strip_w - ix - TAB_CLOSE_W - 10);
    if (label_max_w < 20.0f)
        label_max_w = 20.0f;

    for (size_t i = 0; i < n_tabs && i < max_vis; i++) {
        int y0 = (int)(i * row_h);
        bool editing = (edit_idx == i);

        DrawRectangle(0, y0, ix, row_h, tab_index_bg);
        DrawRectangle(ix - 1, y0, 1, row_h, border);

        Color title_bg = editing ? edit_bg
                               : ((i == active_idx) ? tab_active : tab_bg);
        DrawRectangle(ix, y0, strip_w - ix - 1, tab_title_h, title_bg);

        int y_res = y0 + tab_title_h;
        DrawRectangle(ix, y_res, strip_w - ix - 1, tab_reserved_h,
                      tab_reserved_bg);

        /* Title / reserved: only to the right of the index column — a full-width
         * line here would cut through the vertically centered index digit. */
        DrawRectangle(ix, y0 + tab_title_h - 1, strip_w - ix - 1, 1, border);
        /* Between two tab rows: full width so the index cells are separated too. */
        DrawRectangle(0, y_res + tab_reserved_h - 1, strip_w - 1, 1, border);

        char num[8];
        snprintf(num, sizeof(num), "%zu", i + 1);
        Vector2 ns = MeasureTextEx(font, num, font_size, 0);
        float nx = ((float)ix - ns.x) * 0.5f;
        if (nx < 2.0f)
            nx = 2.0f;
        float ny = (float)y0 + ((float)row_h - ns.y) * 0.5f;
        if (i == active_idx)
            draw_text_synthetic_bold(font, num, (Vector2){nx, ny}, font_size, fg);
        else
            DrawTextEx(font, num, (Vector2){nx, ny}, font_size, 0, fg);

        char line[512];
        if (editing && edit_buf)
            snprintf(line, sizeof(line), "%s", edit_buf);
        else {
            char raw[256];
            tab_display_title(tabs[i], i + 1, raw, sizeof(raw));
            truncate_to_width(font, font_size, raw, label_max_w, line,
                              sizeof(line));
        }

        Vector2 ts = MeasureTextEx(font, line, font_size, 0);
        float tx = (float)ix + 6.0f;
        float ty = (float)y0 + ((float)tab_title_h - ts.y) * 0.5f;
        DrawTextEx(font, line, (Vector2){tx, ty}, font_size, 0, fg);

        if (tab_reserved_h > 0) {
            float status_font = font_size * 0.8f;
            if (status_font < 8.0f)
                status_font = 8.0f;

            char status_raw[96];
            const char *agent_name =
                ghostling_agent_state_agent(&tabs[i]->agent_state);
            if (agent_name[0] != '\0') {
                snprintf(status_raw, sizeof(status_raw), "[%s]: %s",
                         agent_name,
                         ghostling_agent_state_label(&tabs[i]->agent_state));
            } else {
                snprintf(status_raw, sizeof(status_raw), "%s",
                         ghostling_agent_state_label(&tabs[i]->agent_state));
            }

            char status_line[96];
            truncate_to_width(font, status_font, status_raw, label_max_w,
                              status_line, sizeof(status_line));

            Vector2 ss = MeasureTextEx(font, status_line, status_font, 0);
            float sy = (float)y_res + ((float)tab_reserved_h - ss.y) * 0.5f;
            DrawTextEx(font, status_line, (Vector2){tx, sy}, status_font, 0,
                       fg);
        }

        const char *x = "×";
        Vector2 xs = MeasureTextEx(font, x, font_size, 0);
        float cx = (float)(strip_w - TAB_CLOSE_W) +
                   (((float)TAB_CLOSE_W - xs.x) * 0.5f);
        DrawTextEx(font, x, (Vector2){cx, ty}, font_size, 0, fg);
    }

    DrawRectangle(0, new_y0, strip_w - 1, TAB_NEW_H, tab_bg);
    DrawRectangle(0, new_y0, strip_w - 1, 1, border);
    const char *plus = "+";
    float icon_font = tab_icon_font_size(font_size);
    Vector2 ps = MeasureTextEx(font, plus, icon_font, 0);
    draw_text_synthetic_bold(
        font, plus,
        (Vector2){((float)strip_w - ps.x) * 0.5f,
                  (float)new_y0 + ((float)TAB_NEW_H - ps.y) * 0.5f},
        icon_font, fg);
}

void tab_splitter_toggle_draw(Font font, float font_size, int strip_w, int scr_h,
                              bool strip_collapsed, bool show, Color fg)
{
    if (!show)
        return;
    float icon_font = tab_icon_font_size(font_size);
    if (strip_collapsed) {
        int tx, ty, tw, th;
        tab_collapsed_expand_bounds(scr_h, &tx, &ty, &tw, &th);
        const char *ch = ">";
        Vector2 cs = MeasureTextEx(font, ch, icon_font, 0);
        DrawTextEx(font, ch,
                   (Vector2){(float)tx + ((float)tw - cs.x) * 0.5f,
                             (float)ty + ((float)th - cs.y) * 0.5f},
                   icon_font, 0, fg);
        return;
    }
    int tx, ty, tw, th;
    tab_splitter_toggle_bounds(strip_w, scr_h, &tx, &ty, &tw, &th);
    const char *lt = "<";
    Vector2 ls = MeasureTextEx(font, lt, icon_font, 0);
    DrawTextEx(font, lt,
               (Vector2){(float)tx + ((float)tw - ls.x) * 0.5f,
                         (float)ty + ((float)th - ls.y) * 0.5f},
               icon_font, 0, fg);
}
