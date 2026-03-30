// ghostling — platform entry (lives with other C modules under src/c/).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* raylib before Win32 headers: GDI/User declare names (Rectangle, CloseWindow,
 * ShowCursor) that collide with raylib's API if windows.h is included first. */
#include "raylib.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOGDI
#define NOGDI
#endif
#ifndef NOUSER
#define NOUSER
#endif
#include <windows.h>
#else
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <ghostty/vt.h>

#include "font_jetbrains_mono.h"

#include "config_font.h"
#include "effects.h"
#include "tabs.h"
#include "terminal_ui.h"

static void layout_terms(int scr_w, int scr_h, int cell_width, int cell_height,
                         int pad, uint16_t *term_cols, uint16_t *term_rows,
                         int *grid_origin_x, int *grid_origin_y)
{
    *grid_origin_x = TAB_STRIP_W + pad;
    *grid_origin_y = pad;
    int content_w = scr_w - *grid_origin_x - pad;
    int content_h = scr_h - 2 * pad;
    int cols = content_w / cell_width;
    int rows = content_h / cell_height;
    if (cols < 1)
        cols = 1;
    if (rows < 1)
        rows = 1;
    *term_cols = (uint16_t)cols;
    *term_rows = (uint16_t)rows;
}

static void close_tab_at(Tab **tabs, size_t *n_tabs, size_t *active, size_t idx)
{
    if (*n_tabs <= 1)
        return;

    tab_free(tabs[idx]);
    free(tabs[idx]);
    memmove(&tabs[idx], &tabs[idx + 1],
            (*n_tabs - 1 - idx) * sizeof(Tab *));
    (*n_tabs)--;
    tabs[*n_tabs] = NULL;

    if (*active == idx) {
        if (idx >= *n_tabs)
            *active = *n_tabs > 0 ? *n_tabs - 1 : 0;
    } else if (*active > idx) {
        (*active)--;
    }
}

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

    Tab *tab_list[MAX_TABS];
    memset(tab_list, 0, sizeof(tab_list));
    size_t n_tabs = 0;
    size_t active = 0;

    int scr_w = GetScreenWidth();
    int scr_h = GetScreenHeight();
    uint16_t term_cols = 1;
    uint16_t term_rows = 1;
    int grid_origin_x = TAB_STRIP_W + pad;
    int grid_origin_y = pad;
    layout_terms(scr_w, scr_h, cell_width, cell_height, pad, &term_cols,
                 &term_rows, &grid_origin_x, &grid_origin_y);

    tab_list[0] = malloc(sizeof(Tab));
    if (!tab_list[0]) {
        fprintf(stderr, "ghostling: out of memory\n");
        UnloadFont(mono_font);
        CloseWindow();
        return 1;
    }
    if (!tab_start_shell(tab_list[0], term_cols, term_rows, cell_width,
                         cell_height, shell_override)) {
        free(tab_list[0]);
        UnloadFont(mono_font);
        CloseWindow();
        return 1;
    }
    n_tabs = 1;

    GhosttyKeyEncoder key_encoder = NULL;
    GhosttyKeyEvent key_event = NULL;
    GhosttyMouseEncoder mouse_encoder = NULL;
    GhosttyMouseEvent mouse_event = NULL;
    GhosttyRenderState render_state = NULL;
    GhosttyRenderStateRowIterator row_iter = NULL;
    GhosttyRenderStateRowCells row_cells = NULL;
    int exit_code = 0;

    GhosttyResult err = ghostty_key_encoder_new(NULL, &key_encoder);
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

    while (!WindowShouldClose()) {
        scr_w = GetScreenWidth();
        scr_h = GetScreenHeight();

        if (IsWindowResized()) {
            int w = scr_w;
            int h = scr_h;
            if (w != prev_width || h != prev_height) {
                layout_terms(w, h, cell_width, cell_height, pad, &term_cols,
                             &term_rows, &grid_origin_x, &grid_origin_y);
                for (size_t i = 0; i < n_tabs; i++)
                    tab_resize_pty(tab_list[i], term_cols, term_rows, cell_width,
                                   cell_height);
                prev_width = w;
                prev_height = h;
            }
        }

        bool focused = IsWindowFocused();
        if (focused != prev_focused) {
            Tab *cur = tab_list[active];
            bool focus_mode = false;
            if (!cur->child_exited &&
                ghostty_terminal_mode_get(cur->terminal, GHOSTTY_MODE_FOCUS_EVENT,
                                          &focus_mode) == GHOSTTY_SUCCESS &&
                focus_mode) {
                GhosttyFocusEvent focus_event =
                    focused ? GHOSTTY_FOCUS_GAINED : GHOSTTY_FOCUS_LOST;
                char focus_buf[8];
                size_t focus_written = 0;
                GhosttyResult focus_res = ghostty_focus_encode(
                    focus_event, focus_buf, sizeof(focus_buf), &focus_written);
                if (focus_res == GHOSTTY_SUCCESS && focus_written > 0)
                    pty_write(tab_pty_write(cur), focus_buf, focus_written);
            }
            prev_focused = focused;
        }

        for (size_t i = 0; i < n_tabs; i++) {
            if (tab_list[i]->child_exited)
                continue;
            PtyReadResult pr = tab_drain(tab_list[i]);
            if (pr != PTY_READ_OK)
                tab_list[i]->child_exited = true;
        }

        for (size_t i = 0; i < n_tabs; i++) {
            Tab *t = tab_list[i];
            if (!t->child_exited || t->child_reaped)
                continue;
#ifdef _WIN32
            DWORD wstatus = WaitForSingleObject(t->pty_ctx.process, 0);
            if (wstatus == WAIT_OBJECT_0) {
                t->child_reaped = true;
                DWORD code = 0;
                if (GetExitCodeProcess(t->pty_ctx.process, &code))
                    t->child_exit_status = (int)code;
            } else if (wstatus == WAIT_FAILED) {
                t->child_reaped = true;
            }
#else
            int wstatus = 0;
            pid_t wp = waitpid(t->child, &wstatus, WNOHANG);
            if (wp > 0) {
                t->child_reaped = true;
                if (WIFEXITED(wstatus))
                    t->child_exit_status = WEXITSTATUS(wstatus);
                else if (WIFSIGNALED(wstatus))
                    t->child_exit_status = 128 + WTERMSIG(wstatus);
            }
#endif
        }

        Tab *cur = tab_list[active];

        Vector2 mpos = GetMousePosition();
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            mpos.x < (float)TAB_STRIP_W) {
            size_t idx = 0;
            TabStripAction act = TAB_STRIP_NONE;
            if (tab_strip_hit(mpos, scr_h, n_tabs, &idx, &act)) {
                if (act == TAB_STRIP_NEW && n_tabs < MAX_TABS) {
                    layout_terms(scr_w, scr_h, cell_width, cell_height, pad,
                                 &term_cols, &term_rows, &grid_origin_x,
                                 &grid_origin_y);
                    for (size_t i = 0; i < n_tabs; i++)
                        tab_resize_pty(tab_list[i], term_cols, term_rows,
                                       cell_width, cell_height);
                    Tab *nt = malloc(sizeof(Tab));
                    if (nt && tab_start_shell(nt, term_cols, term_rows,
                                              cell_width, cell_height,
                                              shell_override)) {
                        tab_list[n_tabs] = nt;
                        active = n_tabs;
                        n_tabs++;
                        scrollbar_dragging = false;
                    } else if (nt) {
                        free(nt);
                    }
                } else if (act == TAB_STRIP_SELECT) {
                    active = idx;
                    scrollbar_dragging = false;
                } else if (act == TAB_STRIP_CLOSE && n_tabs > 1) {
                    close_tab_at(tab_list, &n_tabs, &active, idx);
                    scrollbar_dragging = false;
                }
            }
        }

        cur = tab_list[active];

        bool scrollbar_consumed = handle_scrollbar(
            cur->terminal, render_state, &scrollbar_dragging, grid_origin_x,
            grid_origin_y, term_rows, cell_height, pad);

        if (!cur->child_exited) {
            handle_input(tab_pty_write(cur), key_encoder, key_event,
                         cur->terminal);
            if (!scrollbar_consumed && mpos.x >= (float)TAB_STRIP_W)
                handle_mouse(tab_pty_write(cur), mouse_encoder, mouse_event,
                             cur->terminal, cell_width, cell_height,
                             grid_origin_x, pad, pad, pad);
        }

        ghostty_render_state_update(render_state, cur->terminal);

        GhosttyRenderStateColors bg_colors =
            GHOSTTY_INIT_SIZED(GhosttyRenderStateColors);
        ghostty_render_state_colors_get(render_state, &bg_colors);
        Color win_bg = {bg_colors.background.r, bg_colors.background.g,
                        bg_colors.background.b, 255};

        GhosttyTerminalScrollbar scrollbar = {0};
        GhosttyTerminalScrollbar *scrollbar_ptr = NULL;
        if (ghostty_terminal_get(cur->terminal, GHOSTTY_TERMINAL_DATA_SCROLLBAR,
                                 &scrollbar) == GHOSTTY_SUCCESS)
            scrollbar_ptr = &scrollbar;

        Color strip_bg = {45, 45, 48, 255};
        Color tab_bg = {55, 55, 58, 255};
        Color tab_active = {70, 100, 140, 255};
        Color border = {80, 80, 85, 255};
        Color tab_fg = {220, 220, 220, 255};

        BeginDrawing();
        ClearBackground(win_bg);
        render_terminal(render_state, row_iter, row_cells, mono_font, cell_width,
                        cell_height, font_size, scrollbar_ptr, grid_origin_x,
                        grid_origin_y, term_rows, pad);
        tab_strip_draw(mono_font, (float)font_size_px, scr_h, n_tabs, active,
                       strip_bg, tab_bg, tab_active, border, tab_fg);

        if (cur->child_exited) {
            char exit_msg[128];
            if (cur->child_exit_status >= 0)
                snprintf(exit_msg, sizeof(exit_msg),
                         "[process exited with status %d]",
                         cur->child_exit_status);
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

    for (size_t i = 0; i < n_tabs; i++) {
        tab_free(tab_list[i]);
        free(tab_list[i]);
    }

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
    return exit_code;
}
