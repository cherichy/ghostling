// ghostling — platform entry (lives with other C modules under src/c/).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdint.h>
#include <time.h>

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
#include <sys/stat.h>
#else
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <ghostty/vt.h>

#include "font_jetbrains_mono.h"

#include "agent_events.h"
#include "config_font.h"
#include "effects.h"
#include "tabs.h"
#include "terminal_ui.h"

bool ghostling_agent_state_run_tests(void);

typedef struct {
    bool enabled;
    bool pressed;
} KeyBindingState;

static bool key_is_down(int key)
{
    return IsKeyDown(key);
}

static bool key_mods_match(uint8_t mods)
{
    bool shift = key_is_down(KEY_LEFT_SHIFT) || key_is_down(KEY_RIGHT_SHIFT);
    bool ctrl = key_is_down(KEY_LEFT_CONTROL) || key_is_down(KEY_RIGHT_CONTROL);
    bool alt = key_is_down(KEY_LEFT_ALT) || key_is_down(KEY_RIGHT_ALT);
    bool super = key_is_down(KEY_LEFT_SUPER) || key_is_down(KEY_RIGHT_SUPER);

    bool want_shift = (mods & GHOSTLING_KEYMOD_SHIFT) != 0;
    bool want_ctrl = (mods & GHOSTLING_KEYMOD_CTRL) != 0;
    bool want_alt = (mods & GHOSTLING_KEYMOD_ALT) != 0;
    bool want_super = (mods & GHOSTLING_KEYMOD_SUPER) != 0;

#if defined(__APPLE__)
    if (mods & GHOSTLING_KEYMOD_PRIMARY)
        want_super = true;
#else
    if (mods & GHOSTLING_KEYMOD_PRIMARY)
        want_ctrl = true;
#endif

    return shift == want_shift && ctrl == want_ctrl && alt == want_alt &&
           super == want_super;
}

static bool key_binding_pressed(const GhostlingKeyBinding *binding,
                                KeyBindingState *state)
{
    if (!binding->enabled) {
        state->pressed = false;
        state->enabled = false;
        return false;
    }

    bool key_down = key_is_down(binding->key);
    bool match = key_down && key_mods_match(binding->mods);
    bool triggered = match && !state->pressed;
    state->pressed = match;
    state->enabled = true;
    return triggered;
}

static bool tab_switch_relative(size_t *active, size_t n_tabs, int delta)
{
    if (n_tabs == 0 || delta == 0)
        return false;

    int idx = (int)(*active);
    int nt = (int)n_tabs;
    idx = (idx + delta) % nt;
    if (idx < 0)
        idx += nt;
    if ((size_t)idx == *active)
        return false;
    *active = (size_t)idx;
    return true;
}

static bool file_mtime_seconds(const char *path, time_t *out)
{
    if (!path || !path[0])
        return false;

    struct stat st;
    if (stat(path, &st) != 0)
        return false;

    *out = st.st_mtime;
    return true;
}

static void update_terminal_metadata(Tab *tab, char *window_title,
                                     size_t window_title_sz)
{
    effect_sync_pwd(tab->terminal, &tab->effects);

    char tab_title[256] = {0};
    tab_display_title(tab, 1, tab_title, sizeof(tab_title));
    if (tab_title[0] != '\0') {
        snprintf(window_title, window_title_sz, "%s", tab_title);
        return;
    }

    if (tab->effects.pwd[0] != '\0') {
        snprintf(window_title, window_title_sz, "%s", tab->effects.pwd);
        return;
    }

    snprintf(window_title, window_title_sz, "%s", "ghostling");
}

static void raylib_trace_filter_callback(int log_level, const char *text,
                                         va_list args)
{
    /* We change target FPS frequently (active/idle/unfocused), and raylib emits
     * an INFO line on every SetTargetFPS() call. Filter only that noisy line while
     * keeping all other diagnostics visible. */
    if (log_level == LOG_INFO &&
        strstr(text, "TIMER: Target time per frame") != NULL)
        return;

    const char *prefix = "LOG";
    if (log_level == LOG_TRACE)
        prefix = "TRACE";
    else if (log_level == LOG_DEBUG)
        prefix = "DEBUG";
    else if (log_level == LOG_INFO)
        prefix = "INFO";
    else if (log_level == LOG_WARNING)
        prefix = "WARNING";
    else if (log_level == LOG_ERROR)
        prefix = "ERROR";
    else if (log_level == LOG_FATAL)
        prefix = "FATAL";

    fprintf(stderr, "%s: ", prefix);
    vfprintf(stderr, text, args);
    fputc('\n', stderr);
}

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

static void tab_selection_clear(Tab *t)
{
    t->selection_active = false;
    t->selection_dragging = false;
}

static void tab_selection_normalize(const Tab *t, uint16_t *x0, uint16_t *y0,
                                    uint16_t *x1, uint16_t *y1)
{
    *x0 = t->selection_anchor_x;
    *y0 = t->selection_anchor_y;
    *x1 = t->selection_focus_x;
    *y1 = t->selection_focus_y;

    if (*y0 > *y1 || (*y0 == *y1 && *x0 > *x1)) {
        uint16_t tx = *x0, ty = *y0;
        *x0 = *x1;
        *y0 = *y1;
        *x1 = tx;
        *y1 = ty;
    }
}

static void mouse_to_cell_clamped(Vector2 mpos, int grid_origin_x,
                                  int grid_origin_y, int term_pixel_w,
                                  int term_pixel_h, int cell_width,
                                  int cell_height, uint16_t term_cols,
                                  uint16_t term_rows, uint16_t *out_x,
                                  uint16_t *out_y)
{
    float x = mpos.x;
    float y = mpos.y;
    float max_x = (float)(grid_origin_x + term_pixel_w - 1);
    float max_y = (float)(grid_origin_y + term_pixel_h - 1);

    if (x < (float)grid_origin_x)
        x = (float)grid_origin_x;
    if (y < (float)grid_origin_y)
        y = (float)grid_origin_y;
    if (x > max_x)
        x = max_x;
    if (y > max_y)
        y = max_y;

    int col = (int)((x - (float)grid_origin_x) / (float)cell_width);
    int row = (int)((y - (float)grid_origin_y) / (float)cell_height);
    if (col < 0)
        col = 0;
    if (row < 0)
        row = 0;
    if (col >= (int)term_cols)
        col = (int)term_cols - 1;
    if (row >= (int)term_rows)
        row = (int)term_rows - 1;

    *out_x = (uint16_t)col;
    *out_y = (uint16_t)row;
}

static bool shortcut_primary_modifier_down(void)
{
#if defined(__APPLE__)
    return IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
#else
    return IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
#endif
}

static bool selection_copy_shortcut_pressed(GhostlingCopyShortcut shortcut)
{
    bool primary = shortcut_primary_modifier_down();
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    if (!primary)
        return false;

    if (shortcut == GHOSTLING_COPY_SHORTCUT_CTRL_SHIFT_C)
        return shift && IsKeyPressed(KEY_C);

    return IsKeyPressed(KEY_C);
}

static bool paste_shortcut_pressed(GhostlingPasteShortcut shortcut)
{
    bool primary = shortcut_primary_modifier_down();
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    if (!primary)
        return false;

    if (shortcut == GHOSTLING_PASTE_SHORTCUT_NONE)
        return false;
    if (shortcut == GHOSTLING_PASTE_SHORTCUT_CTRL_SHIFT_V)
        return shift && IsKeyPressed(KEY_V);

    return IsKeyPressed(KEY_V);
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

static bool reload_terminal_font(Font *font, const AppConfig *cfg,
                                 Vector2 dpi_scale, int *font_size_px,
                                 int *cell_width, int *cell_height)
{
    int cp_count = 0;
    int *codepoints = build_terminal_codepoints(cfg->font_codepoint_set, &cp_count);
    if (!codepoints || cp_count <= 0) {
        free(codepoints);
        return false;
    }

    int new_font_size_px = (int)(cfg->font_size * dpi_scale.y);
    const char *font_path_try = cfg->font_path[0] ? cfg->font_path : NULL;
    Font new_font = load_terminal_font(font_path_try, font_jetbrains_mono,
                                       (int)sizeof(font_jetbrains_mono),
                                       new_font_size_px, codepoints, cp_count);
    free(codepoints);

    if (new_font.glyphCount <= 0 || new_font.texture.id == 0)
        return false;

    SetTextureFilter(new_font.texture, TEXTURE_FILTER_POINT);

    Vector2 glyph_size =
        MeasureTextEx(new_font, "M", (float)new_font_size_px, 0);
    int new_cell_w = (int)((glyph_size.x / dpi_scale.x) + 0.5f);
    int new_cell_h = (int)((glyph_size.y / dpi_scale.y) + 0.5f);
    if (new_cell_w < 1)
        new_cell_w = 1;
    if (new_cell_h < 1)
        new_cell_h = 1;

    UnloadFont(*font);
    *font = new_font;
    *font_size_px = new_font_size_px;
    *cell_width = new_cell_w;
    *cell_height = new_cell_h;

    int atlas_bytes = GetPixelDataSize(font->texture.width, font->texture.height,
                                       font->texture.format);
    fprintf(stderr,
            "ghostling: font atlas set=%s codepoints=%d texture=%dx%d (~%.1f MiB)\n",
            cfg->font_codepoint_set, cp_count, font->texture.width,
            font->texture.height, (double)atlas_bytes / (1024.0 * 1024.0));

    return true;
}

int main(int argc, char *argv[])
{
    if (argc > 1 && strcmp(argv[1], "--selftest-agent-state") == 0) {
        bool ok = ghostling_agent_state_run_tests();
        return ok ? 0 : 1;
    }

    const char *shell_override = (argc > 1) ? argv[1] : NULL;

    SetTraceLogCallback(raylib_trace_filter_callback);

    log_build_info();

    AppConfig app_cfg;
    config_load(&app_cfg);
    int font_size = app_cfg.font_size;

    SetConfigFlags(FLAG_WINDOW_HIGHDPI);

    InitWindow(800, 600, "ghostling");
    /* Do not let raylib treat Escape as "close window".
     * Escape must be delivered to the PTY/app running in the terminal. */
    SetExitKey(KEY_NULL);
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    const int fps_active = 60;
    const int fps_idle = 8;
    const int fps_unfocused = 4;
    const double active_grace_sec = 0.30;
    int target_fps = fps_active;
    SetTargetFPS(target_fps);

    Vector2 dpi_scale = GetWindowScaleDPI();

    int cp_count = 0;
    int *codepoints =
        build_terminal_codepoints(app_cfg.font_codepoint_set, &cp_count);
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

    int atlas_bytes = GetPixelDataSize(mono_font.texture.width,
                                       mono_font.texture.height,
                                       mono_font.texture.format);
    fprintf(stderr,
            "ghostling: font atlas set=%s codepoints=%d texture=%dx%d (~%.1f MiB)\n",
            app_cfg.font_codepoint_set, cp_count, mono_font.texture.width,
            mono_font.texture.height, (double)atlas_bytes / (1024.0 * 1024.0));

    /* Terminal grids are sensitive to 1px color bleed at glyph edges
     * (Powerline separators, box-drawing). Point sampling avoids atlas
     * interpolation artifacts between neighboring glyph texels. */
    SetTextureFilter(mono_font.texture, TEXTURE_FILTER_POINT);

    Vector2 glyph_size = MeasureTextEx(mono_font, "M", (float)font_size_px, 0);
    int cell_width = (int)((glyph_size.x / dpi_scale.x) + 0.5f);
    int cell_height = (int)((glyph_size.y / dpi_scale.y) + 0.5f);
    if (cell_width < 1)
        cell_width = 1;
    if (cell_height < 1)
        cell_height = 1;

    GhostlingHanTier current_han_tier =
        ghostling_han_tier_from_codepoint_set(app_cfg.font_codepoint_set);

    const int pad = 4;

    int tab_strip_w = TAB_STRIP_W_DEFAULT;
    bool tab_strip_collapsed = false;

    Tab *tab_list[MAX_TABS];
    memset(tab_list, 0, sizeof(tab_list));
    size_t n_tabs = 0;
    size_t active = 0;
    AgentEventBus agent_bus;
    agent_event_bus_init(&agent_bus);

    int scr_w = GetScreenWidth();
    int scr_h = GetScreenHeight();
    int render_w = GetRenderWidth();
    int render_h = GetRenderHeight();
    int ui_w = scr_w;
    int ui_h = scr_h;
    /* Prefer render size mapped back to logical coords; avoids oversized scr_h on maximize. */
    if (render_w > 0 && dpi_scale.x > 0.0f) {
        int logical_w = (int)((float)render_w / dpi_scale.x);
        if (logical_w > 0)
            ui_w = logical_w;
    }
    if (render_h > 0 && dpi_scale.y > 0.0f) {
        int logical_h = (int)((float)render_h / dpi_scale.y);
        if (logical_h > 0)
            ui_h = logical_h;
    }
    uint16_t term_cols = 1;
    uint16_t term_rows = 1;
    int grid_origin_x = tab_strip_w + pad;
    int grid_origin_y = pad;
    clamp_tab_strip_w(ui_w, cell_width, pad, &tab_strip_w);
    layout_terms(ui_w, ui_h, tab_strip_layout_w(tab_strip_collapsed, tab_strip_w),
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
    tab_set_agent_state_hook(tab_list[0], agent_event_bus_on_state_change,
                             &agent_bus);
    n_tabs = 1;

    double last_tab_click_t = -100.0;
    size_t last_tab_click_idx = TAB_EDIT_NONE;
    size_t edit_tab = TAB_EDIT_NONE;
    char edit_buf[256];

    bool splitter_dragging = false;
    bool pending_config_reload = false;
    KeyBindingState key_new_tab_state = {0};
    KeyBindingState key_close_tab_state = {0};
    KeyBindingState key_next_tab_state = {0};
    KeyBindingState key_prev_tab_state = {0};
    KeyBindingState key_toggle_tab_strip_state = {0};
    KeyBindingState key_reload_config_state = {0};
    time_t loaded_config_mtime = 0;
    bool has_loaded_config_mtime =
        file_mtime_seconds(app_cfg.loaded_config_path, &loaded_config_mtime);

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

    int prev_width = ui_w;
    int prev_height = ui_h;
    bool prev_focused = IsWindowFocused();
    bool scrollbar_dragging = false;
    double last_activity_t = GetTime();
    Vector2 prev_mouse_pos = GetMousePosition();
    char window_title_cache[280];
    window_title_cache[0] = '\0';

    while (!WindowShouldClose()) {
        bool frame_activity = false;
        scr_w = GetScreenWidth();
        scr_h = GetScreenHeight();
        render_w = GetRenderWidth();
        render_h = GetRenderHeight();

        if (!pending_config_reload && app_cfg.loaded_config_path[0]) {
            time_t now_mtime = 0;
            bool has_now =
                file_mtime_seconds(app_cfg.loaded_config_path, &now_mtime);
            if (has_now && (!has_loaded_config_mtime ||
                            now_mtime != loaded_config_mtime)) {
                pending_config_reload = true;
                frame_activity = true;
            }
        }

        ui_w = scr_w;
        ui_h = scr_h;
        /* Prefer render size mapped back to logical coords; avoids oversized scr_h on maximize. */
        if (render_w > 0 && dpi_scale.x > 0.0f) {
            int logical_w = (int)((float)render_w / dpi_scale.x);
            if (logical_w > 0)
                ui_w = logical_w;
        }
        if (render_h > 0 && dpi_scale.y > 0.0f) {
            int logical_h = (int)((float)render_h / dpi_scale.y);
            if (logical_h > 0)
                ui_h = logical_h;
        }

        /* Do not rely only on IsWindowResized() — on Windows, maximize can change
         * GetScreenWidth/Height without a reliable resize flag, leaving PTY/grid stale
         * while UI draw uses new scr_h (tabs/+ would desync or appear "missing"). */
        if (ui_w != prev_width || ui_h != prev_height) {
            clamp_tab_strip_w(ui_w, cell_width, pad, &tab_strip_w);
            layout_terms(ui_w, ui_h,
                         tab_strip_layout_w(tab_strip_collapsed, tab_strip_w),
                         cell_width, cell_height, pad, &term_cols, &term_rows,
                         &grid_origin_x, &grid_origin_y);
            for (size_t i = 0; i < n_tabs; i++)
                tab_resize_pty(tab_list[i], term_cols, term_rows, cell_width,
                               cell_height);
            prev_width = ui_w;
            prev_height = ui_h;
            frame_activity = true;
        }

        if (splitter_dragging && !tab_strip_collapsed) {
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                int nx = (int)GetMousePosition().x;
                if (nx != tab_strip_w) {
                    tab_strip_w = nx;
                    apply_strip_resize(ui_w, ui_h, &tab_strip_w,
                                       tab_strip_collapsed, cell_width,
                                       cell_height, pad, &term_cols,
                                       &term_rows, &grid_origin_x,
                                       &grid_origin_y, tab_list, n_tabs);
                    frame_activity = true;
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
            frame_activity = true;
        }

        for (size_t i = 0; i < n_tabs; i++) {
            if (tab_list[i]->child_exited)
                continue;
            PtyReadResult pr = tab_drain(tab_list[i]);
            if (pr != PTY_READ_OK) {
                tab_list[i]->child_exited = true;
                frame_activity = true;
            }
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
                tab_agent_state_on_process_exit(t, t->child_exit_status);
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
                tab_agent_state_on_process_exit(t, t->child_exit_status);
            }
#endif
        }

        Tab *cur = tab_list[active];

        if (key_binding_pressed(&app_cfg.key_reload_config,
                                &key_reload_config_state)) {
            pending_config_reload = true;
            frame_activity = true;
        }

        if (edit_tab == TAB_EDIT_NONE) {
            if (key_binding_pressed(&app_cfg.key_toggle_tab_strip,
                                    &key_toggle_tab_strip_state)) {
                tab_strip_collapsed = !tab_strip_collapsed;
                apply_strip_resize(ui_w, ui_h, &tab_strip_w, tab_strip_collapsed,
                                   cell_width, cell_height, pad, &term_cols,
                                   &term_rows, &grid_origin_x, &grid_origin_y,
                                   tab_list, n_tabs);
                frame_activity = true;
            }

            if (key_binding_pressed(&app_cfg.key_next_tab, &key_next_tab_state) &&
                tab_switch_relative(&active, n_tabs, +1))
                frame_activity = true;

            if (key_binding_pressed(&app_cfg.key_prev_tab, &key_prev_tab_state) &&
                tab_switch_relative(&active, n_tabs, -1))
                frame_activity = true;

            if (key_binding_pressed(&app_cfg.key_new_tab, &key_new_tab_state) &&
                n_tabs < MAX_TABS) {
                Tab *nt = malloc(sizeof(Tab));
                if (nt && tab_start_shell(nt, term_cols, term_rows, cell_width,
                                          cell_height, shell_override)) {
                    tab_set_agent_state_hook(nt, agent_event_bus_on_state_change,
                                             &agent_bus);
                    tab_list[n_tabs] = nt;
                    active = n_tabs;
                    n_tabs++;
                    frame_activity = true;
                } else if (nt) {
                    free(nt);
                }
            }

            if (key_binding_pressed(&app_cfg.key_close_tab,
                                    &key_close_tab_state) &&
                n_tabs > 1) {
                close_tab_at(tab_list, &n_tabs, &active, active);
                frame_activity = true;
            }
        }

        Vector2 mpos = GetMousePosition();
        if (mpos.x != prev_mouse_pos.x || mpos.y != prev_mouse_pos.y)
            frame_activity = true;
        prev_mouse_pos = mpos;
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) ||
            IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) ||
            IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE) ||
            IsMouseButtonReleased(MOUSE_BUTTON_LEFT) ||
            IsMouseButtonReleased(MOUSE_BUTTON_RIGHT) ||
            IsMouseButtonReleased(MOUSE_BUTTON_MIDDLE) ||
            IsMouseButtonDown(MOUSE_BUTTON_LEFT) ||
            IsMouseButtonDown(MOUSE_BUTTON_RIGHT) ||
            IsMouseButtonDown(MOUSE_BUTTON_MIDDLE))
            frame_activity = true;

        int strip_w_eff = tab_strip_layout_w(tab_strip_collapsed, tab_strip_w);

        if (tab_splitter_toggle_hit(mpos, strip_w_eff, ui_h))
            SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
        else if (!tab_strip_collapsed &&
                 tab_splitter_hit(mpos, tab_strip_w, ui_h))
            SetMouseCursor(MOUSE_CURSOR_RESIZE_EW);
        else
            SetMouseCursor(MOUSE_CURSOR_DEFAULT);

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (tab_splitter_toggle_hit(mpos, strip_w_eff, ui_h)) {
                tab_strip_collapsed = !tab_strip_collapsed;
                apply_strip_resize(ui_w, ui_h, &tab_strip_w, tab_strip_collapsed,
                                   cell_width, cell_height, pad, &term_cols,
                                   &term_rows, &grid_origin_x, &grid_origin_y,
                                   tab_list, n_tabs);
                splitter_dragging = false;
            } else if (!tab_strip_collapsed &&
                       tab_splitter_hit(mpos, tab_strip_w, ui_h)) {
                splitter_dragging = true;
                scrollbar_dragging = false;
            } else if (mpos.x < (float)strip_w_eff && !splitter_dragging) {
                size_t idx = 0;
                TabStripAction act = TAB_STRIP_NONE;
                if (tab_strip_hit(mpos, strip_w_eff, ui_h, n_tabs, &idx, &act,
                                   app_cfg.tab_title_h, app_cfg.tab_reserved_h,
                                   tab_strip_collapsed)) {
                    if (act == TAB_STRIP_NEW && n_tabs < MAX_TABS) {
                        edit_tab = TAB_EDIT_NONE;
                        apply_strip_resize(ui_w, ui_h, &tab_strip_w,
                                           tab_strip_collapsed, cell_width,
                                           cell_height, pad, &term_cols,
                                           &term_rows, &grid_origin_x,
                                           &grid_origin_y, tab_list, n_tabs);
                        Tab *nt = malloc(sizeof(Tab));
                        if (nt && tab_start_shell(nt, term_cols, term_rows,
                                                  cell_width, cell_height,
                                                  shell_override)) {
                            tab_set_agent_state_hook(
                                nt, agent_event_bus_on_state_change,
                                &agent_bus);
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
            if (IsKeyPressed(KEY_ESCAPE)) {
                edit_tab = TAB_EDIT_NONE;
                frame_activity = true;
            } else if (IsKeyPressed(KEY_ENTER)) {
                snprintf(tab_list[edit_tab]->effects.title_override,
                         sizeof(tab_list[edit_tab]->effects.title_override),
                         "%s", edit_buf);
                edit_tab = TAB_EDIT_NONE;
                frame_activity = true;
            } else {
                if (IsKeyPressed(KEY_BACKSPACE) ||
                    IsKeyPressedRepeat(KEY_BACKSPACE)) {
                    utf8_pop_back(edit_buf);
                    frame_activity = true;
                }
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
                        frame_activity = true;
                    }
                }
            }
        }

        bool scrollbar_consumed = handle_scrollbar(
            cur->terminal, render_state, &scrollbar_dragging, grid_origin_x,
            grid_origin_y, term_rows, cell_height, pad);
        if (scrollbar_consumed || scrollbar_dragging)
            frame_activity = true;

        if (!cur->child_exited && edit_tab == TAB_EDIT_NONE) {
            int term_pixel_w = (int)term_cols * cell_width;
            int term_pixel_h = (int)term_rows * cell_height;
            bool mouse_in_terminal =
                mpos.x >= (float)grid_origin_x &&
                mpos.y >= (float)grid_origin_y &&
                mpos.x < (float)(grid_origin_x + term_pixel_w) &&
                mpos.y < (float)(grid_origin_y + term_pixel_h);

            bool mouse_tracking = false;
            ghostty_terminal_get(cur->terminal, GHOSTTY_TERMINAL_DATA_MOUSE_TRACKING,
                                 &mouse_tracking);

            bool hyperlink_clicked = false;

            if (mouse_in_terminal) {
                uint16_t hover_x = 0;
                uint16_t hover_y = 0;
                mouse_to_cell_clamped(mpos, grid_origin_x, grid_origin_y,
                                      term_pixel_w, term_pixel_h, cell_width,
                                      cell_height, term_cols, term_rows,
                                      &hover_x, &hover_y);
                if (cell_has_hyperlink(cur->terminal, hover_x, hover_y))
                    SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);

                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
                    shortcut_primary_modifier_down()) {
                    if (open_url_at_cell(cur->terminal, term_cols, term_rows,
                                         hover_x, hover_y)) {
                        hyperlink_clicked = true;
                        frame_activity = true;
                    }
                }
            }

            if (!hyperlink_clicked && !mouse_tracking && !scrollbar_consumed) {
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    if (mouse_in_terminal &&
                        !(tab_strip_collapsed &&
                          tab_splitter_toggle_hit(mpos, 0, ui_h))) {
                        uint16_t sel_x = 0, sel_y = 0;
                        mouse_to_cell_clamped(mpos, grid_origin_x, grid_origin_y,
                                              term_pixel_w, term_pixel_h,
                                              cell_width, cell_height, term_cols,
                                              term_rows, &sel_x, &sel_y);
                        cur->selection_anchor_x = sel_x;
                        cur->selection_anchor_y = sel_y;
                        cur->selection_focus_x = sel_x;
                        cur->selection_focus_y = sel_y;
                        cur->selection_active = true;
                        cur->selection_dragging = true;
                        frame_activity = true;
                    } else if (cur->selection_active) {
                        tab_selection_clear(cur);
                        frame_activity = true;
                    }
                }

                if (cur->selection_dragging && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                    uint16_t sel_x = cur->selection_focus_x;
                    uint16_t sel_y = cur->selection_focus_y;
                    mouse_to_cell_clamped(mpos, grid_origin_x, grid_origin_y,
                                          term_pixel_w, term_pixel_h, cell_width,
                                          cell_height, term_cols, term_rows,
                                          &sel_x, &sel_y);
                    if (sel_x != cur->selection_focus_x ||
                        sel_y != cur->selection_focus_y) {
                        cur->selection_focus_x = sel_x;
                        cur->selection_focus_y = sel_y;
                        frame_activity = true;
                    }
                }

                if (cur->selection_dragging &&
                    IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                    cur->selection_dragging = false;
                    frame_activity = true;

                    if (app_cfg.selection_copy_on_select &&
                        cur->selection_active) {
                        uint16_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
                        tab_selection_normalize(cur, &x0, &y0, &x1, &y1);
                        if (copy_viewport_selection_to_clipboard(
                                cur->terminal, term_cols, term_rows, x0, y0,
                                x1, y1))
                            frame_activity = true;
                    }
                }
            }

            bool copy_triggered =
                selection_copy_shortcut_pressed(app_cfg.selection_copy_shortcut) &&
                cur->selection_active;
            bool copied_shortcut = copy_triggered;
            if (copy_triggered) {
                uint16_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
                tab_selection_normalize(cur, &x0, &y0, &x1, &y1);
                if (copy_viewport_selection_to_clipboard(cur->terminal, term_cols,
                                                         term_rows, x0, y0, x1,
                                                         y1)) {
                    tab_selection_clear(cur);
                    frame_activity = true;
                }
            }

            bool paste_triggered = paste_shortcut_pressed(app_cfg.paste_shortcut);
            bool pasted_shortcut = paste_triggered;
            if (paste_triggered) {
                if (paste_host_clipboard_to_terminal(tab_pty_write(cur),
                                                     cur->terminal)) {
                    tab_agent_state_on_local_input(cur);
                    tab_selection_clear(cur);
                    frame_activity = true;
                }
            }

            if (!copied_shortcut && !pasted_shortcut &&
                handle_input(tab_pty_write(cur), key_encoder, key_event,
                             cur->terminal)) {
                tab_agent_state_on_local_input(cur);
                frame_activity = true;
            }

            int mouse_pad_right = ui_w - grid_origin_x - term_pixel_w;
            int mouse_pad_bottom = ui_h - grid_origin_y - term_pixel_h;
            if (mouse_pad_right < 0)
                mouse_pad_right = 0;
            if (mouse_pad_bottom < 0)
                mouse_pad_bottom = 0;

            if (focused && !hyperlink_clicked && !scrollbar_consumed &&
                mouse_in_terminal &&
                !(tab_strip_collapsed &&
                  tab_splitter_toggle_hit(mpos, 0, ui_h)))
                if (handle_mouse(tab_pty_write(cur), mouse_encoder, mouse_event,
                                 cur->terminal, cell_width, cell_height,
                                 grid_origin_x, grid_origin_y,
                                 mouse_pad_right, mouse_pad_bottom, ui_w,
                                 ui_h))
                    frame_activity = true;
        }

        ghostty_render_state_update(render_state, cur->terminal);
        GhosttyRenderStateDirty render_dirty = GHOSTTY_RENDER_STATE_DIRTY_FALSE;
        if (ghostty_render_state_get(render_state, GHOSTTY_RENDER_STATE_DATA_DIRTY,
                                     &render_dirty) == GHOSTTY_SUCCESS &&
            render_dirty != GHOSTTY_RENDER_STATE_DIRTY_FALSE)
            frame_activity = true;

        char wtitle[280];
        update_terminal_metadata(cur, wtitle, sizeof(wtitle));
        if (strcmp(window_title_cache, wtitle) != 0) {
            SetWindowTitle(wtitle);
            snprintf(window_title_cache, sizeof(window_title_cache), "%s",
                     wtitle);
            frame_activity = true;
        }

        double now_t = GetTime();
        if (frame_activity)
            last_activity_t = now_t;
        bool active_recent = (now_t - last_activity_t) < active_grace_sec;
        int desired_fps = focused ? (active_recent ? fps_active : fps_idle)
                                  : (active_recent ? fps_idle : fps_unfocused);
        if (desired_fps != target_fps) {
            SetTargetFPS(desired_fps);
            target_fps = desired_fps;
        }

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

        bool selection_active = cur->selection_active;
        uint16_t sel_x0 = 0, sel_y0 = 0, sel_x1 = 0, sel_y1 = 0;
        if (selection_active)
            tab_selection_normalize(cur, &sel_x0, &sel_y0, &sel_x1, &sel_y1);

        BeginDrawing();
        ClearBackground(win_bg);
        GhostlingHanTier missing_han_tier = render_terminal(
            render_state, row_iter, row_cells, mono_font, cell_width,
            cell_height, font_size, scrollbar_ptr, grid_origin_x, grid_origin_y,
            term_rows, pad, selection_active, sel_x0, sel_y0, sel_x1, sel_y1,
            current_han_tier);
        /* Match render_terminal: logical size for DrawTextEx vs GetScreen* coords. */
        float tab_title_font_px =
            (float)font_size * app_cfg.tab_title_font_scale;
        if (tab_title_font_px < 6.0f)
            tab_title_font_px = 6.0f;
        tab_strip_draw(mono_font, tab_title_font_px,
                       tab_strip_layout_w(tab_strip_collapsed, tab_strip_w),
                       ui_h, tab_list, n_tabs, active, edit_tab, edit_buf,
                       strip_bg, tab_index_bg, tab_reserved_bg, tab_bg,
                       tab_active, border, tab_fg, edit_bg, app_cfg.tab_title_h,
                       app_cfg.tab_reserved_h, tab_strip_collapsed);

        if (!tab_strip_collapsed &&
            (splitter_dragging || tab_splitter_hit(mpos, tab_strip_w, ui_h))) {
            int sx = tab_strip_w - 1;
            DrawRectangle(sx, 0, 2, ui_h, (Color){120, 160, 220, 255});
        }

        tab_splitter_toggle_draw(
            mono_font, tab_title_font_px,
            tab_strip_layout_w(tab_strip_collapsed, tab_strip_w), ui_h,
            tab_strip_collapsed,
            tab_splitter_toggle_hit(mpos, tab_strip_collapsed ? 0 : tab_strip_w,
                                    ui_h),
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

        if (pending_config_reload) {
            AppConfig loaded;
            config_load_profile(&loaded, app_cfg.profile);

            bool key_reload_disabled = false;
            if (!loaded.key_reload_config.enabled)
                key_reload_disabled = true;

            bool font_set_changed =
                strcmp(loaded.font_codepoint_set, app_cfg.font_codepoint_set) != 0;
            bool font_path_changed = strcmp(loaded.font_path, app_cfg.font_path) != 0;
            bool font_size_changed = loaded.font_size != app_cfg.font_size;

            app_cfg = loaded;
            font_size = app_cfg.font_size;
            has_loaded_config_mtime =
                file_mtime_seconds(app_cfg.loaded_config_path,
                                  &loaded_config_mtime);

            if (font_set_changed || font_path_changed || font_size_changed) {
                if (reload_terminal_font(&mono_font, &app_cfg, dpi_scale,
                                         &font_size_px, &cell_width,
                                         &cell_height)) {
                    current_han_tier = ghostling_han_tier_from_codepoint_set(
                        app_cfg.font_codepoint_set);
                    clamp_tab_strip_w(ui_w, cell_width, pad, &tab_strip_w);
                    layout_terms(ui_w, ui_h,
                                 tab_strip_layout_w(tab_strip_collapsed,
                                                    tab_strip_w),
                                 cell_width, cell_height, pad, &term_cols,
                                 &term_rows, &grid_origin_x, &grid_origin_y);
                    for (size_t i = 0; i < n_tabs; i++)
                        tab_resize_pty(tab_list[i], term_cols, term_rows,
                                       cell_width, cell_height);
                }
            }

            if (key_reload_disabled)
                key_reload_config_state.pressed = false;
            pending_config_reload = false;
        }

        if (current_han_tier != GHOSTLING_HAN_TIER_NONE &&
            missing_han_tier > current_han_tier) {
            GhostlingHanTier next_han_tier = current_han_tier;
            if (current_han_tier == GHOSTLING_HAN_TIER_3500)
                next_han_tier = GHOSTLING_HAN_TIER_6500;
            else if (current_han_tier == GHOSTLING_HAN_TIER_6500)
                next_han_tier = GHOSTLING_HAN_TIER_8105;

            const char *next_set =
                ghostling_codepoint_set_for_han_tier(next_han_tier);
            if (next_set) {
                snprintf(app_cfg.font_codepoint_set,
                         sizeof(app_cfg.font_codepoint_set), "%s", next_set);

                if (reload_terminal_font(&mono_font, &app_cfg, dpi_scale,
                                         &font_size_px, &cell_width,
                                         &cell_height)) {
                    current_han_tier = next_han_tier;
                    frame_activity = true;
                    clamp_tab_strip_w(ui_w, cell_width, pad, &tab_strip_w);
                    layout_terms(ui_w, ui_h,
                                 tab_strip_layout_w(tab_strip_collapsed,
                                                    tab_strip_w),
                                 cell_width, cell_height, pad, &term_cols,
                                 &term_rows, &grid_origin_x, &grid_origin_y);
                    for (size_t i = 0; i < n_tabs; i++)
                        tab_resize_pty(tab_list[i], term_cols, term_rows,
                                       cell_width, cell_height);
                    fprintf(stderr,
                            "ghostling: upgraded Han table tier to %s after glyph miss\n",
                            next_set);
                }
            }
        }
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
