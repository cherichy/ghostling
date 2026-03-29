// ghostling — platform entry (lives with other C modules under src/c/).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include "pty_win.h"
#include <windows.h>
#else
#include "pty_unix.h"
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "raylib.h"
#include <ghostty/vt.h>

#include "font_jetbrains_mono.h"

#include "config_font.h"
#include "effects.h"
#include "terminal_ui.h"

int main(int argc, char *argv[])
{
    const char *shell_override = (argc > 1) ? argv[1] : NULL;

    log_build_info();

    AppConfig app_cfg;
    config_load(&app_cfg);
    int font_size = app_cfg.font_size;

    SetConfigFlags(FLAG_WINDOW_HIGHDPI);

    InitWindow(800, 600, "ghostling");
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    SetTargetFPS(60);

    Vector2 dpi_scale = GetWindowScaleDPI();

    int cp_count = 0;
    int *codepoints = build_terminal_codepoints(&cp_count);
    if (!codepoints || cp_count <= 0) {
        fprintf(stderr, "ghostling: out of memory building codepoint list\n");
        CloseWindow();
        return 1;
    }

    int font_size_px = (int)(font_size * dpi_scale.y);
    const char *font_path_try = app_cfg.font_path[0] ? app_cfg.font_path : NULL;
    Font mono_font = load_terminal_font(
        font_path_try, font_jetbrains_mono, (int)sizeof(font_jetbrains_mono),
        font_size_px, codepoints, cp_count);
    free(codepoints);

    SetTextureFilter(mono_font.texture, TEXTURE_FILTER_BILINEAR);

    Vector2 glyph_size = MeasureTextEx(mono_font, "M", (float)font_size_px, 0);
    int cell_width = (int)(glyph_size.x / dpi_scale.x);
    int cell_height = (int)(glyph_size.y / dpi_scale.y);
    if (cell_width < 1)
        cell_width = 1;
    if (cell_height < 1)
        cell_height = 1;

    const int pad = 4;

    int scr_w = GetScreenWidth();
    int scr_h = GetScreenHeight();
    uint16_t term_cols = (uint16_t)((scr_w - 2 * pad) / cell_width);
    uint16_t term_rows = (uint16_t)((scr_h - 2 * pad) / cell_height);
    if (term_cols < 1)
        term_cols = 1;
    if (term_rows < 1)
        term_rows = 1;

    GhosttyTerminal terminal = NULL;
#ifdef _WIN32
    PtyContext pty_ctx = {.hpc = INVALID_HANDLE_VALUE,
                          .process = INVALID_HANDLE_VALUE,
                          .pipe_in = INVALID_HANDLE_VALUE,
                          .pipe_out = INVALID_HANDLE_VALUE};
    PtyReadBuf pty_rb = {0};
    InitializeCriticalSection(&pty_rb.cs);
    HANDLE pty_reader = NULL;
#else
    pid_t child = -1;
    int pty_fd = -1;
#endif
    GhosttyKeyEncoder key_encoder = NULL;
    GhosttyKeyEvent key_event = NULL;
    GhosttyMouseEncoder mouse_encoder = NULL;
    GhosttyMouseEvent mouse_event = NULL;
    GhosttyRenderState render_state = NULL;
    GhosttyRenderStateRowIterator row_iter = NULL;
    GhosttyRenderStateRowCells row_cells = NULL;
    int exit_code = 0;

    GhosttyTerminalOptions opts = {.cols = term_cols,
                                   .rows = term_rows,
                                   .max_scrollback = 1000};
    GhosttyResult err = ghostty_terminal_new(NULL, &terminal, opts);
    if (err != GHOSTTY_SUCCESS) {
        fprintf(stderr, "ghostty_terminal_new failed (%d)\n", err);
        exit_code = 1;
        goto cleanup;
    }

#ifdef _WIN32
    if (!pty_spawn_win32(&pty_ctx, term_cols, term_rows, shell_override)) {
        exit_code = 1;
        goto cleanup;
    }
    pty_rb.pipe = pty_ctx.pipe_out;
    pty_reader = CreateThread(NULL, 0, pty_reader_thread, &pty_rb, 0, NULL);
    if (!pty_reader) {
        win_perror("CreateThread (pty reader)");
        exit_code = 1;
        goto cleanup;
    }
    PtyHandle pty_wr = pty_ctx.pipe_in;
#else
    pty_fd = pty_spawn_unix(&child, term_cols, term_rows, shell_override,
                            cell_width, cell_height);
    if (pty_fd < 0) {
        exit_code = 1;
        goto cleanup;
    }
    PtyHandle pty_wr = pty_fd;
#endif

#ifdef _WIN32
    EffectsContext effects_ctx = {.pty_fd = pty_ctx.pipe_in,
#else
    EffectsContext effects_ctx = {.pty_fd = pty_fd,
#endif
                                  .cell_width = cell_width,
                                  .cell_height = cell_height,
                                  .cols = term_cols,
                                  .rows = term_rows};

    ghostty_terminal_set(terminal, GHOSTTY_TERMINAL_OPT_USERDATA, &effects_ctx);

    ghostty_terminal_set(terminal, GHOSTTY_TERMINAL_OPT_WRITE_PTY,
                         (const void *)effect_write_pty);
    ghostty_terminal_set(terminal, GHOSTTY_TERMINAL_OPT_SIZE,
                         (const void *)effect_size);
    ghostty_terminal_set(terminal, GHOSTTY_TERMINAL_OPT_DEVICE_ATTRIBUTES,
                         (const void *)effect_device_attributes);
    ghostty_terminal_set(terminal, GHOSTTY_TERMINAL_OPT_XTVERSION,
                         (const void *)effect_xtversion);
    ghostty_terminal_set(terminal, GHOSTTY_TERMINAL_OPT_TITLE_CHANGED,
                         (const void *)effect_title_changed);
    ghostty_terminal_set(terminal, GHOSTTY_TERMINAL_OPT_COLOR_SCHEME,
                         (const void *)effect_color_scheme);

    err = ghostty_key_encoder_new(NULL, &key_encoder);
    if (err != GHOSTTY_SUCCESS) {
        fprintf(stderr, "ghostty_key_encoder_new failed (%d)\n", err);
        exit_code = 1;
        goto cleanup;
    }

    err = ghostty_key_event_new(NULL, &key_event);
    if (err != GHOSTTY_SUCCESS) {
        fprintf(stderr, "ghostty_key_event_new failed (%d)\n", err);
        exit_code = 1;
        goto cleanup;
    }

    err = ghostty_mouse_encoder_new(NULL, &mouse_encoder);
    if (err != GHOSTTY_SUCCESS) {
        fprintf(stderr, "ghostty_mouse_encoder_new failed (%d)\n", err);
        exit_code = 1;
        goto cleanup;
    }

    err = ghostty_mouse_event_new(NULL, &mouse_event);
    if (err != GHOSTTY_SUCCESS) {
        fprintf(stderr, "ghostty_mouse_event_new failed (%d)\n", err);
        exit_code = 1;
        goto cleanup;
    }

    err = ghostty_render_state_new(NULL, &render_state);
    if (err != GHOSTTY_SUCCESS) {
        fprintf(stderr, "ghostty_render_state_new failed (%d)\n", err);
        exit_code = 1;
        goto cleanup;
    }

    err = ghostty_render_state_row_iterator_new(NULL, &row_iter);
    if (err != GHOSTTY_SUCCESS) {
        fprintf(stderr, "ghostty_render_state_row_iterator_new failed (%d)\n",
                err);
        exit_code = 1;
        goto cleanup;
    }

    err = ghostty_render_state_row_cells_new(NULL, &row_cells);
    if (err != GHOSTTY_SUCCESS) {
        fprintf(stderr, "ghostty_render_state_row_cells_new failed (%d)\n", err);
        exit_code = 1;
        goto cleanup;
    }

    int prev_width = scr_w;
    int prev_height = scr_h;
    bool prev_focused = IsWindowFocused();
    bool scrollbar_dragging = false;
    bool child_exited = false;
    bool child_reaped = false;
    int child_exit_status = -1;

    while (!WindowShouldClose()) {
        if (IsWindowResized()) {
            int w = GetScreenWidth();
            int h = GetScreenHeight();
            if (w != prev_width || h != prev_height) {
                int cols = (w - 2 * pad) / cell_width;
                int rows = (h - 2 * pad) / cell_height;
                if (cols < 1)
                    cols = 1;
                if (rows < 1)
                    rows = 1;
                term_cols = (uint16_t)cols;
                term_rows = (uint16_t)rows;
                ghostty_terminal_resize(terminal, term_cols, term_rows,
                                        (uint32_t)cell_width,
                                        (uint32_t)cell_height);
                effects_ctx.cols = term_cols;
                effects_ctx.rows = term_rows;
#ifdef _WIN32
                pty_resize_win32(pty_ctx.hpc, term_cols, term_rows);
#else
                struct winsize new_ws = {
                    .ws_row = term_rows,
                    .ws_col = term_cols,
                    .ws_xpixel = (unsigned short)(term_cols * cell_width),
                    .ws_ypixel = (unsigned short)(term_rows * cell_height),
                };
                ioctl(pty_fd, TIOCSWINSZ, &new_ws);
#endif
                prev_width = w;
                prev_height = h;
            }
        }

        bool focused = IsWindowFocused();
        if (focused != prev_focused) {
            bool focus_mode = false;
            if (!child_exited &&
                ghostty_terminal_mode_get(terminal, GHOSTTY_MODE_FOCUS_EVENT,
                                          &focus_mode) == GHOSTTY_SUCCESS &&
                focus_mode) {
                GhosttyFocusEvent focus_event =
                    focused ? GHOSTTY_FOCUS_GAINED : GHOSTTY_FOCUS_LOST;
                char focus_buf[8];
                size_t focus_written = 0;
                GhosttyResult focus_res = ghostty_focus_encode(
                    focus_event, focus_buf, sizeof(focus_buf), &focus_written);
                if (focus_res == GHOSTTY_SUCCESS && focus_written > 0)
                    pty_write(pty_wr, focus_buf, focus_written);
            }
            prev_focused = focused;
        }

        if (!child_exited) {
#ifdef _WIN32
            PtyReadResult pty_rc = pty_buf_drain(&pty_rb, terminal);
#else
            PtyReadResult pty_rc = pty_read_unix(pty_fd, terminal);
#endif
            if (pty_rc != PTY_READ_OK)
                child_exited = true;
        }

        if (child_exited && !child_reaped) {
#ifdef _WIN32
            DWORD wstatus = WaitForSingleObject(pty_ctx.process, 0);
            if (wstatus == WAIT_OBJECT_0) {
                child_reaped = true;
                DWORD code = 0;
                if (GetExitCodeProcess(pty_ctx.process, &code))
                    child_exit_status = (int)code;
            } else if (wstatus == WAIT_FAILED) {
                child_reaped = true;
            }
#else
            int wstatus = 0;
            pid_t wp = waitpid(child, &wstatus, WNOHANG);
            if (wp > 0) {
                child_reaped = true;
                if (WIFEXITED(wstatus))
                    child_exit_status = WEXITSTATUS(wstatus);
                else if (WIFSIGNALED(wstatus))
                    child_exit_status = 128 + WTERMSIG(wstatus);
            }
#endif
        }

        bool scrollbar_consumed =
            handle_scrollbar(terminal, render_state, &scrollbar_dragging);

        if (!child_exited) {
            handle_input(pty_wr, key_encoder, key_event, terminal);
            if (!scrollbar_consumed)
                handle_mouse(pty_wr, mouse_encoder, mouse_event, terminal,
                             cell_width, cell_height, pad);
        }

        ghostty_render_state_update(render_state, terminal);

        GhosttyRenderStateColors bg_colors =
            GHOSTTY_INIT_SIZED(GhosttyRenderStateColors);
        ghostty_render_state_colors_get(render_state, &bg_colors);
        Color win_bg = {bg_colors.background.r, bg_colors.background.g,
                        bg_colors.background.b, 255};

        GhosttyTerminalScrollbar scrollbar = {0};
        GhosttyTerminalScrollbar *scrollbar_ptr = NULL;
        if (ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_SCROLLBAR,
                                 &scrollbar) == GHOSTTY_SUCCESS)
            scrollbar_ptr = &scrollbar;

        BeginDrawing();
        ClearBackground(win_bg);
        render_terminal(render_state, row_iter, row_cells, mono_font,
                        cell_width, cell_height, font_size, scrollbar_ptr);

        if (child_exited) {
            char exit_msg[128];
            if (child_exit_status >= 0)
                snprintf(exit_msg, sizeof(exit_msg),
                         "[process exited with status %d]", child_exit_status);
            else
                snprintf(exit_msg, sizeof(exit_msg), "[process exited]");

            Vector2 msg_size =
                MeasureTextEx(mono_font, exit_msg, (float)font_size, 0);
            int screen_w = GetScreenWidth();
            int screen_h = GetScreenHeight();
            int banner_h = (int)msg_size.y + 8;
            DrawRectangle(0, screen_h - banner_h, screen_w, banner_h,
                          (Color){0, 0, 0, 180});
            DrawTextEx(mono_font, exit_msg,
                       (Vector2){(screen_w - msg_size.x) / 2,
                                 screen_h - banner_h + 4},
                       (float)font_size, 0, WHITE);
        }

        EndDrawing();
    }

cleanup:
    UnloadFont(mono_font);
    CloseWindow();
#ifdef _WIN32
    if (pty_ctx.hpc != INVALID_HANDLE_VALUE) {
        ClosePseudoConsole(pty_ctx.hpc);
        pty_ctx.hpc = INVALID_HANDLE_VALUE;
    }
    if (pty_reader) {
        if (WaitForSingleObject(pty_reader, 3000) != WAIT_OBJECT_0) {
            fprintf(stderr,
                    "pty reader thread did not exit in time, terminating\n");
            TerminateThread(pty_reader, 1);
        }
        CloseHandle(pty_reader);
    }
    pty_cleanup_win(&pty_ctx);
    DeleteCriticalSection(&pty_rb.cs);
#else
    if (pty_fd >= 0)
        close(pty_fd);
    if (child > 0 && !child_reaped) {
        if (!child_exited)
            kill(child, SIGHUP);
        waitpid(child, NULL, 0);
    }
#endif
    if (mouse_event)
        ghostty_mouse_event_free(mouse_event);
    if (mouse_encoder)
        ghostty_mouse_encoder_free(mouse_encoder);
    if (key_event)
        ghostty_key_event_free(key_event);
    if (key_encoder)
        ghostty_key_encoder_free(key_encoder);
    if (row_cells)
        ghostty_render_state_row_cells_free(row_cells);
    if (row_iter)
        ghostty_render_state_row_iterator_free(row_iter);
    if (render_state)
        ghostty_render_state_free(render_state);
    if (terminal)
        ghostty_terminal_free(terminal);
    return exit_code;
}
