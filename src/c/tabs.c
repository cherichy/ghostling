#include "tabs.h"

#include <stdio.h>
#include <string.h>

#ifndef _WIN32
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

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
#ifdef _WIN32
    t->pty_ctx.hpc = INVALID_HANDLE_VALUE;
    t->pty_ctx.process = INVALID_HANDLE_VALUE;
    t->pty_ctx.pipe_in = INVALID_HANDLE_VALUE;
    t->pty_ctx.pipe_out = INVALID_HANDLE_VALUE;
    InitializeCriticalSection(&t->pty_rb.cs);
    t->pty_cs_inited = true;
#endif
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
    t->effects.title_override[0] = '\0';

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
#ifdef _WIN32
    return pty_buf_drain(&t->pty_rb, t->terminal);
#else
    return pty_read_unix(t->pty_fd, t->terminal);
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

/** Shared geometry for the collapse/expand affordance (splitter, vertical center). */
static void tab_splitter_toggle_bounds(int effective_strip_w, int scr_h, int *tx,
                                     int *ty, int *tw, int *th)
{
    *tw = 20;
    *th = 26;
    *tx = effective_strip_w - *tw / 2;
    *ty = scr_h / 2 - *th / 2;
}

/** Collapsed: small rect near left edge, vertically centered (matches tab_splitter_toggle_draw). */
static void tab_collapsed_expand_bounds(int scr_h, int *tx, int *ty, int *tw, int *th)
{
    *tw = 20;
    *th = 26;
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

        const char *x = "×";
        Vector2 xs = MeasureTextEx(font, x, font_size, 0);
        float cx = (float)(strip_w - TAB_CLOSE_W) +
                   (((float)TAB_CLOSE_W - xs.x) * 0.5f);
        DrawTextEx(font, x, (Vector2){cx, ty}, font_size, 0, fg);
    }

    DrawRectangle(0, new_y0, strip_w - 1, TAB_NEW_H, tab_bg);
    DrawRectangle(0, new_y0, strip_w - 1, 1, border);
    const char *plus = "+";
    Vector2 ps = MeasureTextEx(font, plus, font_size, 0);
    draw_text_synthetic_bold(font, plus, (Vector2){((float)strip_w - ps.x) * 0.5f, (float)new_y0 + ((float)TAB_NEW_H - ps.y) * 0.5f}, font_size, fg);
}

void tab_splitter_toggle_draw(Font font, float font_size, int strip_w, int scr_h,
                              bool strip_collapsed, bool show, Color fg)
{
    if (!show)
        return;
    if (strip_collapsed) {
        int tx, ty, tw, th;
        tab_collapsed_expand_bounds(scr_h, &tx, &ty, &tw, &th);
        const char *ch = ">";
        Vector2 cs = MeasureTextEx(font, ch, font_size, 0);
        DrawTextEx(font, ch,
                   (Vector2){(float)tx + ((float)tw - cs.x) * 0.5f,
                             (float)ty + ((float)th - cs.y) * 0.5f},
                   font_size, 0, fg);
        return;
    }
    int tx, ty, tw, th;
    tab_splitter_toggle_bounds(strip_w, scr_h, &tx, &ty, &tw, &th);
    const char *lt = "<";
    Vector2 ls = MeasureTextEx(font, lt, font_size, 0);
    DrawTextEx(font, lt,
               (Vector2){(float)tx + ((float)tw - ls.x) * 0.5f,
                         (float)ty + ((float)th - ls.y) * 0.5f},
               font_size, 0, fg);
}
