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

        if (out_len == sizeof(out))
            TAB_FLUSH_OUT();
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
