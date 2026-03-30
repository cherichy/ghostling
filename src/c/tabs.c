#include "tabs.h"

#include <stdio.h>
#include <string.h>

#ifndef _WIN32
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

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

bool tab_strip_hit(Vector2 mpos, int scr_h, size_t n_tabs, size_t *idx,
                   TabStripAction *act)
{
    *act = TAB_STRIP_NONE;
    if (mpos.x < 0.0f || mpos.x >= (float)TAB_STRIP_W)
        return false;

    int new_y0 = scr_h - TAB_NEW_H;
    if (mpos.y >= (float)new_y0) {
        *act = TAB_STRIP_NEW;
        return true;
    }

    int row = (int)(mpos.y / (float)TAB_ROW_H);
    if (row < 0) {
        *act = TAB_STRIP_NONE;
        return true;
    }

    size_t max_vis = (size_t)((new_y0 > 0) ? (new_y0 / TAB_ROW_H) : 1);
    if (max_vis == 0)
        max_vis = 1;

    if ((size_t)row >= n_tabs || (size_t)row >= max_vis) {
        *act = TAB_STRIP_NONE;
        return true;
    }

    *idx = (size_t)row;
    if (mpos.x >= (float)(TAB_STRIP_W - TAB_CLOSE_W))
        *act = TAB_STRIP_CLOSE;
    else
        *act = TAB_STRIP_SELECT;
    return true;
}

void tab_strip_draw(Font font, float font_size, int scr_h, size_t n_tabs,
                    size_t active_idx, Color strip_bg, Color tab_bg,
                    Color tab_active, Color border, Color fg)
{
    DrawRectangle(0, 0, TAB_STRIP_W, scr_h, strip_bg);
    DrawRectangle(TAB_STRIP_W - 1, 0, 1, scr_h, border);

    int new_y0 = scr_h - TAB_NEW_H;
    size_t max_vis = (size_t)((new_y0 > 0) ? (new_y0 / TAB_ROW_H) : 1);
    if (max_vis == 0)
        max_vis = 1;

    for (size_t i = 0; i < n_tabs && i < max_vis; i++) {
        int y0 = (int)(i * TAB_ROW_H);
        Color bg = (i == active_idx) ? tab_active : tab_bg;
        DrawRectangle(0, y0, TAB_STRIP_W - 1, TAB_ROW_H, bg);
        DrawRectangle(0, y0 + TAB_ROW_H - 1, TAB_STRIP_W - 1, 1, border);

        char label[32];
        snprintf(label, sizeof(label), " %zu ", i + 1);
        Vector2 ts = MeasureTextEx(font, label, font_size, 0);
        float tx = 6.0f;
        float ty = (float)y0 + ((float)TAB_ROW_H - ts.y) * 0.5f;
        DrawTextEx(font, label, (Vector2){tx, ty}, font_size, 0, fg);

        const char *x = "×";
        Vector2 xs = MeasureTextEx(font, x, font_size, 0);
        float cx = (float)(TAB_STRIP_W - TAB_CLOSE_W) +
                   (((float)TAB_CLOSE_W - xs.x) * 0.5f);
        DrawTextEx(font, x, (Vector2){cx, ty}, font_size, 0, fg);
    }

    DrawRectangle(0, new_y0, TAB_STRIP_W - 1, TAB_NEW_H, tab_bg);
    DrawRectangle(0, new_y0, TAB_STRIP_W - 1, 1, border);
    const char *plus = "+";
    Vector2 ps = MeasureTextEx(font, plus, font_size, 0);
    DrawTextEx(font, plus,
               (Vector2){((float)TAB_STRIP_W - ps.x) * 0.5f,
                         (float)new_y0 + ((float)TAB_NEW_H - ps.y) * 0.5f},
               font_size, 0, fg);
}
