// ghostling — platform entry (lives with other C modules under src/c/).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdint.h>

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

static int utf8_encode(uint32_t cp, char out[4])
{
    const uint32_t MAX_UNICODE = 0x10FFFF;
    const uint32_t REPLACEMENT_CHAR = 0xFFFD;

    if (cp > MAX_UNICODE)
        cp = REPLACEMENT_CHAR;

    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

static void utf8_pop_back(char *s)
{
    size_t n = strlen(s);
    if (n == 0)
        return;
    n--;
    while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80)
        n--;
    s[n] = '\0';
}

static void clamp_tab_strip_w(int scr_w, int cell_width, int pad, int *strip_w)
{
    int min_content_px = cell_width * 20;
    int max_strip = scr_w - pad * 2 - min_content_px;
    if (max_strip < TAB_STRIP_W_MIN)
        max_strip = TAB_STRIP_W_MIN;
    if (*strip_w < TAB_STRIP_W_MIN)
        *strip_w = TAB_STRIP_W_MIN;
    if (*strip_w > max_strip)
        *strip_w = max_strip;
}

static int tab_strip_layout_w(bool collapsed, int expanded_w)
{
    return collapsed ? 0 : expanded_w;
}

static void layout_terms(int scr_w, int scr_h, int tab_strip_w, int cell_width,
                         int cell_height, int pad, uint16_t *term_cols,
                         uint16_t *term_rows, int *grid_origin_x,
                         int *grid_origin_y)
{
    *grid_origin_x = tab_strip_w + pad;
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

static void apply_strip_resize(int scr_w, int scr_h, int *tab_strip_w,
                               bool collapsed, int cell_width, int cell_height,
                               int pad, uint16_t *term_cols, uint16_t *term_rows,
                               int *grid_origin_x, int *grid_origin_y,
                               Tab **tab_list, size_t n_tabs)
{
    clamp_tab_strip_w(scr_w, cell_width, pad, tab_strip_w);
    int lw = tab_strip_layout_w(collapsed, *tab_strip_w);
    layout_terms(scr_w, scr_h, lw, cell_width, cell_height, pad, term_cols,
                 term_rows, grid_origin_x, grid_origin_y);
    for (size_t i = 0; i < n_tabs; i++)
        tab_resize_pty(tab_list[i], *term_cols, *term_rows, cell_width,
                       cell_height);
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

    /* Atlas at physical px; DrawTextEx in render_terminal uses logical font_size
     * so glyph scale matches GetScreenWidth()/cell grid under FLAG_WINDOW_HIGHDPI. */
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

    int tab_strip_w = TAB_STRIP_W_DEFAULT;
    bool tab_strip_collapsed = false;

    Tab *tab_list[MAX_TABS];
    memset(tab_list, 0, sizeof(tab_list));
    size_t n_tabs = 0;
    size_t active = 0;

    int scr_w = GetScreenWidth();
    int scr_h = GetScreenHeight();
    uint16_t term_cols = 1;
    uint16_t term_rows = 1;
    int grid_origin_x = tab_strip_w + pad;
    int grid_origin_y = pad;
    clamp_tab_strip_w(scr_w, cell_width, pad, &tab_strip_w);
    layout_terms(scr_w, scr_h, tab_strip_layout_w(tab_strip_collapsed, tab_strip_w),
                 cell_width, cell_height, pad, &term_cols, &term_rows,
                 &grid_origin_x, &grid_origin_y);

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

    double last_tab_click_t = -100.0;
    size_t last_tab_click_idx = TAB_EDIT_NONE;
    size_t edit_tab = TAB_EDIT_NONE;
    char edit_buf[256];

    bool splitter_dragging = false;

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
                clamp_tab_strip_w(w, cell_width, pad, &tab_strip_w);
                layout_terms(w, h,
                             tab_strip_layout_w(tab_strip_collapsed, tab_strip_w),
                             cell_width, cell_height, pad, &term_cols,
                             &term_rows, &grid_origin_x, &grid_origin_y);
                for (size_t i = 0; i < n_tabs; i++)
                    tab_resize_pty(tab_list[i], term_cols, term_rows, cell_width,
                                   cell_height);
                prev_width = w;
                prev_height = h;
            }
        }

        if (splitter_dragging && !tab_strip_collapsed) {
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                int nx = (int)GetMousePosition().x;
                if (nx != tab_strip_w) {
                    tab_strip_w = nx;
                    apply_strip_resize(scr_w, scr_h, &tab_strip_w,
                                       tab_strip_collapsed, cell_width,
                                       cell_height, pad, &term_cols,
                                       &term_rows, &grid_origin_x,
                                       &grid_origin_y, tab_list, n_tabs);
                }
            } else {
                splitter_dragging = false;
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

        int strip_w_eff = tab_strip_layout_w(tab_strip_collapsed, tab_strip_w);

        if (tab_splitter_toggle_hit(mpos, strip_w_eff, scr_h))
            SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
        else if (!tab_strip_collapsed &&
                 tab_splitter_hit(mpos, tab_strip_w, scr_h))
            SetMouseCursor(MOUSE_CURSOR_RESIZE_EW);
        else
            SetMouseCursor(MOUSE_CURSOR_DEFAULT);

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (tab_splitter_toggle_hit(mpos, strip_w_eff, scr_h)) {
                tab_strip_collapsed = !tab_strip_collapsed;
                apply_strip_resize(scr_w, scr_h, &tab_strip_w, tab_strip_collapsed,
                                   cell_width, cell_height, pad, &term_cols,
                                   &term_rows, &grid_origin_x, &grid_origin_y,
                                   tab_list, n_tabs);
                splitter_dragging = false;
            } else if (!tab_strip_collapsed &&
                       tab_splitter_hit(mpos, tab_strip_w, scr_h)) {
                splitter_dragging = true;
                scrollbar_dragging = false;
            } else if (mpos.x < (float)strip_w_eff && !splitter_dragging) {
                size_t idx = 0;
                TabStripAction act = TAB_STRIP_NONE;
                if (tab_strip_hit(mpos, strip_w_eff, scr_h, n_tabs, &idx, &act,
                                   app_cfg.tab_title_h, app_cfg.tab_reserved_h,
                                   tab_strip_collapsed)) {
                    if (act == TAB_STRIP_NEW && n_tabs < MAX_TABS) {
                        edit_tab = TAB_EDIT_NONE;
                        apply_strip_resize(scr_w, scr_h, &tab_strip_w,
                                           tab_strip_collapsed, cell_width,
                                           cell_height, pad, &term_cols,
                                           &term_rows, &grid_origin_x,
                                           &grid_origin_y, tab_list, n_tabs);
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
                    } else if (act == TAB_STRIP_CLOSE && n_tabs > 1) {
                        if (edit_tab == idx)
                            edit_tab = TAB_EDIT_NONE;
                        else if (edit_tab != TAB_EDIT_NONE && edit_tab > idx)
                            edit_tab--;
                        close_tab_at(tab_list, &n_tabs, &active, idx);
                        scrollbar_dragging = false;
                    } else if (act == TAB_STRIP_SELECT) {
                        double now = GetTime();
                        if (now - last_tab_click_t < 0.35 &&
                            idx == last_tab_click_idx && idx == active) {
                            edit_tab = idx;
                            tab_display_title(tab_list[idx], idx + 1, edit_buf,
                                              sizeof(edit_buf));
                            last_tab_click_t = -100.0;
                        } else {
                            edit_tab = TAB_EDIT_NONE;
                            active = idx;
                            last_tab_click_t = now;
                            last_tab_click_idx = idx;
                        }
                        scrollbar_dragging = false;
                    }
                }
            }
        }

        strip_w_eff = tab_strip_layout_w(tab_strip_collapsed, tab_strip_w);

        if (edit_tab != TAB_EDIT_NONE &&
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            mpos.x >= (float)grid_origin_x) {
            edit_tab = TAB_EDIT_NONE;
        }

        cur = tab_list[active];

        if (edit_tab != TAB_EDIT_NONE) {
            if (IsKeyPressed(KEY_ESCAPE))
                edit_tab = TAB_EDIT_NONE;
            else if (IsKeyPressed(KEY_ENTER)) {
                snprintf(tab_list[edit_tab]->effects.title_override,
                         sizeof(tab_list[edit_tab]->effects.title_override),
                         "%s", edit_buf);
                edit_tab = TAB_EDIT_NONE;
            } else {
                if (IsKeyPressed(KEY_BACKSPACE) ||
                    IsKeyPressedRepeat(KEY_BACKSPACE))
                    utf8_pop_back(edit_buf);
                int ch;
                while ((ch = GetCharPressed()) != 0) {
                    size_t len = strlen(edit_buf);
                    if (len + 4 >= sizeof(edit_buf))
                        break;
                    char u8[4];
                    int n = utf8_encode((uint32_t)ch, u8);
                    if (len + (size_t)n < sizeof(edit_buf)) {
                        memcpy(edit_buf + len, u8, (size_t)n);
                        edit_buf[len + (size_t)n] = '\0';
                    }
                }
            }
        }

        bool scrollbar_consumed = handle_scrollbar(
            cur->terminal, render_state, &scrollbar_dragging, grid_origin_x,
            grid_origin_y, term_rows, cell_height, pad);

        if (!cur->child_exited && edit_tab == TAB_EDIT_NONE) {
            handle_input(tab_pty_write(cur), key_encoder, key_event,
                         cur->terminal);
            if (!scrollbar_consumed && mpos.x >= (float)grid_origin_x &&
                !(tab_strip_collapsed &&
                  tab_splitter_toggle_hit(mpos, 0, scr_h)))
                handle_mouse(tab_pty_write(cur), mouse_encoder, mouse_event,
                             cur->terminal, cell_width, cell_height,
                             grid_origin_x, pad, pad, pad);
        }

        ghostty_render_state_update(render_state, cur->terminal);

        char wtitle[280];
        char disp[256];
        tab_display_title(cur, active + 1, disp, sizeof(disp));
        snprintf(wtitle, sizeof(wtitle), "%s", disp[0] != '\0' ? disp : "ghostling");
        SetWindowTitle(wtitle);

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
        Color tab_index_bg = {40, 40, 44, 255};
        Color tab_reserved_bg = {38, 38, 42, 255};
        Color tab_bg = {55, 55, 58, 255};
        Color tab_active = {70, 100, 140, 255};
        Color border = {80, 80, 85, 255};
        Color tab_fg = {220, 220, 220, 255};
        Color edit_bg = {50, 70, 95, 255};

        BeginDrawing();
        ClearBackground(win_bg);
        render_terminal(render_state, row_iter, row_cells, mono_font, cell_width,
                        cell_height, font_size, scrollbar_ptr, grid_origin_x,
                        grid_origin_y, term_rows, pad);
        float tab_title_font_px =
            (float)font_size_px * app_cfg.tab_title_font_scale;
        if (tab_title_font_px < 6.0f)
            tab_title_font_px = 6.0f;
        tab_strip_draw(mono_font, tab_title_font_px,
                       tab_strip_layout_w(tab_strip_collapsed, tab_strip_w),
                       scr_h, tab_list, n_tabs, active, edit_tab, edit_buf,
                       strip_bg, tab_index_bg, tab_reserved_bg, tab_bg,
                       tab_active, border, tab_fg, edit_bg, app_cfg.tab_title_h,
                       app_cfg.tab_reserved_h, tab_strip_collapsed);

        if (!tab_strip_collapsed &&
            (splitter_dragging || tab_splitter_hit(mpos, tab_strip_w, scr_h))) {
            int sx = tab_strip_w - 1;
            DrawRectangle(sx, 0, 2, scr_h, (Color){120, 160, 220, 255});
        }

        tab_splitter_toggle_draw(
            mono_font, tab_title_font_px,
            tab_strip_layout_w(tab_strip_collapsed, tab_strip_w), scr_h,
            tab_strip_collapsed,
            tab_splitter_toggle_hit(mpos, tab_strip_collapsed ? 0 : tab_strip_w,
                                    scr_h),
            tab_fg);

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
