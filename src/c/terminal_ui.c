#include "terminal_ui.h"
#include "pty_common.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

static GhosttyKey raylib_key_to_ghostty(int rl_key)
{
    if (rl_key >= KEY_A && rl_key <= KEY_Z)
        return GHOSTTY_KEY_A + (rl_key - KEY_A);

    if (rl_key >= KEY_ZERO && rl_key <= KEY_NINE)
        return GHOSTTY_KEY_DIGIT_0 + (rl_key - KEY_ZERO);

    if (rl_key >= KEY_F1 && rl_key <= KEY_F12)
        return GHOSTTY_KEY_F1 + (rl_key - KEY_F1);

    switch (rl_key) {
    case KEY_SPACE:
        return GHOSTTY_KEY_SPACE;
    case KEY_ENTER:
        return GHOSTTY_KEY_ENTER;
    case KEY_TAB:
        return GHOSTTY_KEY_TAB;
    case KEY_BACKSPACE:
        return GHOSTTY_KEY_BACKSPACE;
    case KEY_DELETE:
        return GHOSTTY_KEY_DELETE;
    case KEY_ESCAPE:
        return GHOSTTY_KEY_ESCAPE;
    case KEY_UP:
        return GHOSTTY_KEY_ARROW_UP;
    case KEY_DOWN:
        return GHOSTTY_KEY_ARROW_DOWN;
    case KEY_LEFT:
        return GHOSTTY_KEY_ARROW_LEFT;
    case KEY_RIGHT:
        return GHOSTTY_KEY_ARROW_RIGHT;
    case KEY_HOME:
        return GHOSTTY_KEY_HOME;
    case KEY_END:
        return GHOSTTY_KEY_END;
    case KEY_PAGE_UP:
        return GHOSTTY_KEY_PAGE_UP;
    case KEY_PAGE_DOWN:
        return GHOSTTY_KEY_PAGE_DOWN;
    case KEY_INSERT:
        return GHOSTTY_KEY_INSERT;
    case KEY_MINUS:
        return GHOSTTY_KEY_MINUS;
    case KEY_EQUAL:
        return GHOSTTY_KEY_EQUAL;
    case KEY_LEFT_BRACKET:
        return GHOSTTY_KEY_BRACKET_LEFT;
    case KEY_RIGHT_BRACKET:
        return GHOSTTY_KEY_BRACKET_RIGHT;
    case KEY_BACKSLASH:
        return GHOSTTY_KEY_BACKSLASH;
    case KEY_SEMICOLON:
        return GHOSTTY_KEY_SEMICOLON;
    case KEY_APOSTROPHE:
        return GHOSTTY_KEY_QUOTE;
    case KEY_COMMA:
        return GHOSTTY_KEY_COMMA;
    case KEY_PERIOD:
        return GHOSTTY_KEY_PERIOD;
    case KEY_SLASH:
        return GHOSTTY_KEY_SLASH;
    case KEY_GRAVE:
        return GHOSTTY_KEY_BACKQUOTE;
    default:
        return GHOSTTY_KEY_UNIDENTIFIED;
    }
}

static GhosttyMods get_ghostty_mods(void)
{
    GhosttyMods mods = 0;
    if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT))
        mods |= GHOSTTY_MODS_SHIFT;
    if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))
        mods |= GHOSTTY_MODS_CTRL;
    if (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT))
        mods |= GHOSTTY_MODS_ALT;
    if (IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER))
        mods |= GHOSTTY_MODS_SUPER;
    return mods;
}

static uint32_t raylib_key_unshifted_codepoint(int rl_key)
{
    if (rl_key >= KEY_A && rl_key <= KEY_Z)
        return 'a' + (uint32_t)(rl_key - KEY_A);
    if (rl_key >= KEY_ZERO && rl_key <= KEY_NINE)
        return '0' + (uint32_t)(rl_key - KEY_ZERO);

    switch (rl_key) {
    case KEY_SPACE:
        return ' ';
    case KEY_MINUS:
        return '-';
    case KEY_EQUAL:
        return '=';
    case KEY_LEFT_BRACKET:
        return '[';
    case KEY_RIGHT_BRACKET:
        return ']';
    case KEY_BACKSLASH:
        return '\\';
    case KEY_SEMICOLON:
        return ';';
    case KEY_APOSTROPHE:
        return '\'';
    case KEY_COMMA:
        return ',';
    case KEY_PERIOD:
        return '.';
    case KEY_SLASH:
        return '/';
    case KEY_GRAVE:
        return '`';
    default:
        return 0;
    }
}

static int utf8_encode(uint32_t cp, char out[4])
{
    const uint32_t MAX_UNICODE = 0x10FFFF;
    const uint32_t REPLACEMENT_CHAR = 0xFFFD;

    if (cp > MAX_UNICODE) {
        cp = REPLACEMENT_CHAR;
    }

    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    } else if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    } else if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    } else {
        out[0] = (char)(0xF0 | (cp >> 18));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
}

static GhosttyMouseButton raylib_mouse_to_ghostty(int rl_button)
{
    switch (rl_button) {
    case MOUSE_BUTTON_LEFT:
        return GHOSTTY_MOUSE_BUTTON_LEFT;
    case MOUSE_BUTTON_RIGHT:
        return GHOSTTY_MOUSE_BUTTON_RIGHT;
    case MOUSE_BUTTON_MIDDLE:
        return GHOSTTY_MOUSE_BUTTON_MIDDLE;
    case MOUSE_BUTTON_SIDE:
        return GHOSTTY_MOUSE_BUTTON_FOUR;
    case MOUSE_BUTTON_EXTRA:
        return GHOSTTY_MOUSE_BUTTON_FIVE;
    case MOUSE_BUTTON_FORWARD:
        return GHOSTTY_MOUSE_BUTTON_SIX;
    case MOUSE_BUTTON_BACK:
        return GHOSTTY_MOUSE_BUTTON_SEVEN;
    default:
        return GHOSTTY_MOUSE_BUTTON_UNKNOWN;
    }
}

static void mouse_encode_and_write(PtyHandle pty_fd, GhosttyMouseEncoder encoder,
                                   GhosttyMouseEvent event)
{
    char buf[128];
    size_t written = 0;
    GhosttyResult res = ghostty_mouse_encoder_encode(
        encoder, event, buf, sizeof(buf), &written);
    if (res == GHOSTTY_SUCCESS && written > 0)
        pty_write(pty_fd, buf, written);
}

bool handle_mouse(PtyHandle pty_fd, GhosttyMouseEncoder encoder,
                  GhosttyMouseEvent event, GhosttyTerminal terminal,
                  int cell_width, int cell_height, int pad_left, int pad_top,
                  int pad_right, int pad_bottom)
{
    bool had_event = false;
    ghostty_mouse_encoder_setopt_from_terminal(encoder, terminal);

    int scr_w = GetScreenWidth();
    int scr_h = GetScreenHeight();
    GhosttyMouseEncoderSize enc_size = {
        .size = sizeof(GhosttyMouseEncoderSize),
        .screen_width = (uint32_t)scr_w,
        .screen_height = (uint32_t)scr_h,
        .cell_width = (uint32_t)cell_width,
        .cell_height = (uint32_t)cell_height,
        .padding_top = (uint32_t)pad_top,
        .padding_bottom = (uint32_t)pad_bottom,
        .padding_left = (uint32_t)pad_left,
        .padding_right = (uint32_t)pad_right,
    };
    ghostty_mouse_encoder_setopt(encoder, GHOSTTY_MOUSE_ENCODER_OPT_SIZE,
                                 &enc_size);

    bool any_pressed = IsMouseButtonDown(MOUSE_BUTTON_LEFT) ||
                       IsMouseButtonDown(MOUSE_BUTTON_RIGHT) ||
                       IsMouseButtonDown(MOUSE_BUTTON_MIDDLE);
    ghostty_mouse_encoder_setopt(
        encoder, GHOSTTY_MOUSE_ENCODER_OPT_ANY_BUTTON_PRESSED, &any_pressed);

    bool track_cell = true;
    ghostty_mouse_encoder_setopt(
        encoder, GHOSTTY_MOUSE_ENCODER_OPT_TRACK_LAST_CELL, &track_cell);

    GhosttyMods mods = get_ghostty_mods();
    Vector2 pos = GetMousePosition();
    ghostty_mouse_event_set_mods(event, mods);
    ghostty_mouse_event_set_position(
        event, (GhosttyMousePosition){.x = pos.x, .y = pos.y});

    static const int buttons[] = {
        MOUSE_BUTTON_LEFT,   MOUSE_BUTTON_RIGHT,  MOUSE_BUTTON_MIDDLE,
        MOUSE_BUTTON_SIDE,   MOUSE_BUTTON_EXTRA,  MOUSE_BUTTON_FORWARD,
        MOUSE_BUTTON_BACK,
    };
    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
        int rl_btn = buttons[i];
        GhosttyMouseButton gbtn = raylib_mouse_to_ghostty(rl_btn);
        if (gbtn == GHOSTTY_MOUSE_BUTTON_UNKNOWN)
            continue;

        if (IsMouseButtonPressed(rl_btn)) {
            ghostty_mouse_event_set_action(event, GHOSTTY_MOUSE_ACTION_PRESS);
            ghostty_mouse_event_set_button(event, gbtn);
            mouse_encode_and_write(pty_fd, encoder, event);
            had_event = true;
        } else if (IsMouseButtonReleased(rl_btn)) {
            ghostty_mouse_event_set_action(event, GHOSTTY_MOUSE_ACTION_RELEASE);
            ghostty_mouse_event_set_button(event, gbtn);
            mouse_encode_and_write(pty_fd, encoder, event);
            had_event = true;
        }
    }

    Vector2 delta = GetMouseDelta();
    if (delta.x != 0.0f || delta.y != 0.0f) {
        ghostty_mouse_event_set_action(event, GHOSTTY_MOUSE_ACTION_MOTION);
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
            ghostty_mouse_event_set_button(event, GHOSTTY_MOUSE_BUTTON_LEFT);
        else if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
            ghostty_mouse_event_set_button(event, GHOSTTY_MOUSE_BUTTON_RIGHT);
        else if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE))
            ghostty_mouse_event_set_button(event, GHOSTTY_MOUSE_BUTTON_MIDDLE);
        else
            ghostty_mouse_event_clear_button(event);
        mouse_encode_and_write(pty_fd, encoder, event);
        had_event = true;
    }

    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        bool mouse_tracking = false;
        ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_MOUSE_TRACKING,
                             &mouse_tracking);

        if (mouse_tracking) {
            GhosttyMouseButton scroll_btn = (wheel > 0.0f)
                                                ? GHOSTTY_MOUSE_BUTTON_FOUR
                                                : GHOSTTY_MOUSE_BUTTON_FIVE;
            ghostty_mouse_event_set_button(event, scroll_btn);
            ghostty_mouse_event_set_action(event, GHOSTTY_MOUSE_ACTION_PRESS);
            mouse_encode_and_write(pty_fd, encoder, event);
            ghostty_mouse_event_set_action(event, GHOSTTY_MOUSE_ACTION_RELEASE);
            mouse_encode_and_write(pty_fd, encoder, event);
        } else {
            int scroll_delta = (wheel > 0.0f) ? -3 : 3;
            GhosttyTerminalScrollViewport sv = {
                .tag = GHOSTTY_SCROLL_VIEWPORT_DELTA,
                .value = {.delta = scroll_delta},
            };
            ghostty_terminal_scroll_viewport(terminal, sv);
        }
        had_event = true;
    }

    return had_event;
}

bool handle_input(PtyHandle pty_fd, GhosttyKeyEncoder encoder,
                  GhosttyKeyEvent event, GhosttyTerminal terminal)
{
    bool had_event = false;
    ghostty_key_encoder_setopt_from_terminal(encoder, terminal);

    char char_utf8[64];
    int char_utf8_len = 0;
    int ch;
    while ((ch = GetCharPressed()) != 0) {
        char u8[4];
        int n = utf8_encode((uint32_t)ch, u8);
        if (char_utf8_len + n < (int)sizeof(char_utf8)) {
            memcpy(&char_utf8[char_utf8_len], u8, (size_t)n);
            char_utf8_len += n;
            had_event = true;
        }
    }

    static const int special_keys[] = {
        KEY_SPACE,       KEY_ENTER,       KEY_TAB,         KEY_BACKSPACE,
        KEY_DELETE,      KEY_ESCAPE,      KEY_UP,          KEY_DOWN,
        KEY_LEFT,        KEY_RIGHT,       KEY_HOME,        KEY_END,
        KEY_PAGE_UP,     KEY_PAGE_DOWN,   KEY_INSERT,      KEY_MINUS,
        KEY_EQUAL,       KEY_LEFT_BRACKET, KEY_RIGHT_BRACKET, KEY_BACKSLASH,
        KEY_SEMICOLON,   KEY_APOSTROPHE,  KEY_COMMA,       KEY_PERIOD,
        KEY_SLASH,       KEY_GRAVE,       KEY_F1,          KEY_F2,
        KEY_F3,          KEY_F4,          KEY_F5,          KEY_F6,
        KEY_F7,          KEY_F8,          KEY_F9,          KEY_F10,
        KEY_F11,          KEY_F12,
    };

    int keys_to_check[26 + 10 + (int)(sizeof(special_keys) / sizeof(special_keys[0]))];
    int num_keys = 0;
    for (int k = KEY_A; k <= KEY_Z; k++)
        keys_to_check[num_keys++] = k;
    for (int k = KEY_ZERO; k <= KEY_NINE; k++)
        keys_to_check[num_keys++] = k;
    for (size_t i = 0; i < sizeof(special_keys) / sizeof(special_keys[0]); i++)
        keys_to_check[num_keys++] = special_keys[i];

    GhosttyMods mods = get_ghostty_mods();

    for (int i = 0; i < num_keys; i++) {
        int rl_key = keys_to_check[i];
        bool pressed = IsKeyPressed(rl_key);
        bool repeated = IsKeyPressedRepeat(rl_key);
        bool released = IsKeyReleased(rl_key);
        if (!pressed && !repeated && !released)
            continue;
        had_event = true;

        GhosttyKey gkey = raylib_key_to_ghostty(rl_key);
        if (gkey == GHOSTTY_KEY_UNIDENTIFIED)
            continue;

        GhosttyKeyAction action =
            released ? GHOSTTY_KEY_ACTION_RELEASE
                     : (pressed ? GHOSTTY_KEY_ACTION_PRESS
                                : GHOSTTY_KEY_ACTION_REPEAT);

        ghostty_key_event_set_key(event, gkey);
        ghostty_key_event_set_action(event, action);
        ghostty_key_event_set_mods(event, mods);

        uint32_t ucp = raylib_key_unshifted_codepoint(rl_key);
        ghostty_key_event_set_unshifted_codepoint(event, ucp);

        GhosttyMods consumed = 0;
        if (ucp != 0 && (mods & GHOSTTY_MODS_SHIFT))
            consumed |= GHOSTTY_MODS_SHIFT;
        ghostty_key_event_set_consumed_mods(event, consumed);

        if (char_utf8_len > 0 && !released) {
            ghostty_key_event_set_utf8(event, char_utf8, (size_t)char_utf8_len);
            char_utf8_len = 0;
        } else {
            ghostty_key_event_set_utf8(event, NULL, 0);
        }

        char buf[128];
        size_t written = 0;
        GhosttyResult res = ghostty_key_encoder_encode(
            encoder, event, buf, sizeof(buf), &written);
        if (res == GHOSTTY_SUCCESS && written > 0) {
            pty_write(pty_fd, buf, written);
            char_utf8_len = 0;
        }
    }

    if (char_utf8_len > 0)
        pty_write(pty_fd, char_utf8, (size_t)char_utf8_len);

    return had_event;
}

bool handle_scrollbar(GhosttyTerminal terminal, GhosttyRenderState render_state,
                      bool *dragging, int grid_origin_x, int grid_origin_y,
                      uint16_t term_rows, int cell_height, int pad_right)
{
    Vector2 mpos = GetMousePosition();
    if (mpos.x < (float)grid_origin_x) {
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
            *dragging = false;
        return false;
    }

    GhosttyTerminalScrollbar scrollbar = {0};
    if (ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_SCROLLBAR,
                             &scrollbar) != GHOSTTY_SUCCESS)
        return false;

    if (scrollbar.total <= scrollbar.len) {
        *dragging = false;
        return false;
    }

    int scr_w = GetScreenWidth();
    int track_h = (int)term_rows * cell_height;
    if (track_h < 1)
        track_h = 1;

    const int bar_width = 6;
    const int bar_margin = 2;
    int bar_left = scr_w - pad_right - bar_width - bar_margin;
    int hit_left = bar_left - 8;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && mpos.x >= hit_left &&
        mpos.x <= scr_w && mpos.y >= (float)grid_origin_y &&
        mpos.y <= (float)(grid_origin_y + track_h)) {
        *dragging = true;
    }

    if (*dragging && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        uint64_t scrollable = scrollbar.total - scrollbar.len;
        double frac =
            ((double)mpos.y - (double)grid_origin_y) / (double)track_h;
        if (frac < 0.0)
            frac = 0.0;
        if (frac > 1.0)
            frac = 1.0;
        int64_t target = (int64_t)(frac * (double)scrollable);

        intptr_t delta = (intptr_t)(target - (int64_t)scrollbar.offset);
        if (delta != 0) {
            GhosttyTerminalScrollViewport sv = {
                .tag = GHOSTTY_SCROLL_VIEWPORT_DELTA,
                .value = {.delta = delta},
            };
            ghostty_terminal_scroll_viewport(terminal, sv);
            ghostty_render_state_update(render_state, terminal);
        }
    }

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        *dragging = false;

    return *dragging;
}

void render_terminal(GhosttyRenderState render_state,
                     GhosttyRenderStateRowIterator row_iter,
                     GhosttyRenderStateRowCells cells, Font font,
                     int cell_width, int cell_height, int font_size,
                     const GhosttyTerminalScrollbar *scrollbar, int grid_origin_x,
                     int grid_origin_y, uint16_t term_rows, int pad_right)
{
    GhosttyRenderStateColors colors = GHOSTTY_INIT_SIZED(GhosttyRenderStateColors);
    if (ghostty_render_state_colors_get(render_state, &colors) !=
        GHOSTTY_SUCCESS)
        return;

    if (ghostty_render_state_get(render_state,
                                 GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR,
                                 &row_iter) != GHOSTTY_SUCCESS)
        return;

    int y = grid_origin_y;

    while (ghostty_render_state_row_iterator_next(row_iter)) {
        if (ghostty_render_state_row_get(row_iter,
                                         GHOSTTY_RENDER_STATE_ROW_DATA_CELLS,
                                         &cells) != GHOSTTY_SUCCESS)
            continue;

        int x = grid_origin_x;

        while (ghostty_render_state_row_cells_next(cells)) {
            uint32_t grapheme_len = 0;
            ghostty_render_state_row_cells_get(
                cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_LEN,
                &grapheme_len);

            if (grapheme_len == 0) {
                GhosttyColorRgb bg = {0};
                if (ghostty_render_state_row_cells_get(
                        cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_BG_COLOR,
                        &bg) == GHOSTTY_SUCCESS) {
                    DrawRectangle(x, y, cell_width, cell_height,
                                  (Color){bg.r, bg.g, bg.b, 255});
                }

                x += cell_width;
                continue;
            }

            uint32_t codepoints[16];
            uint32_t len = grapheme_len < 16 ? grapheme_len : 16;
            ghostty_render_state_row_cells_get(
                cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_BUF,
                codepoints);

            char text[64];
            int pos = 0;
            for (uint32_t i = 0; i < len && pos < 60; i++) {
                char u8[4];
                int n = utf8_encode(codepoints[i], u8);
                memcpy(&text[pos], u8, (size_t)n);
                pos += n;
            }
            text[pos] = '\0';

            GhosttyColorRgb fg = colors.foreground;
            ghostty_render_state_row_cells_get(
                cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_FG_COLOR, &fg);

            GhosttyColorRgb bg_rgb = colors.background;
            bool has_bg = ghostty_render_state_row_cells_get(
                              cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_BG_COLOR,
                              &bg_rgb) == GHOSTTY_SUCCESS;

            GhosttyStyle style = GHOSTTY_INIT_SIZED(GhosttyStyle);
            ghostty_render_state_row_cells_get(
                cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_STYLE, &style);

            if (style.inverse) {
                GhosttyColorRgb tmp = fg;
                fg = bg_rgb;
                bg_rgb = tmp;
                has_bg = true;
            }

            Color ray_fg = {fg.r, fg.g, fg.b, 255};

            if (has_bg) {
                DrawRectangle(x, y, cell_width, cell_height,
                              (Color){bg_rgb.r, bg_rgb.g, bg_rgb.b, 255});
            }

            int italic_offset = style.italic ? (font_size / 6) : 0;

            DrawTextEx(font, text, (Vector2){(float)(x + italic_offset), (float)y},
                       (float)font_size, 0, ray_fg);

            if (style.bold) {
                DrawTextEx(font, text,
                           (Vector2){(float)(x + italic_offset + 1), (float)y},
                           (float)font_size, 0, ray_fg);
            }

            x += cell_width;
        }

        bool clean = false;
        ghostty_render_state_row_set(row_iter,
                                     GHOSTTY_RENDER_STATE_ROW_OPTION_DIRTY,
                                     &clean);

        y += cell_height;
    }

    bool cursor_visible = false;
    ghostty_render_state_get(render_state,
                             GHOSTTY_RENDER_STATE_DATA_CURSOR_VISIBLE,
                             &cursor_visible);
    bool cursor_in_viewport = false;
    ghostty_render_state_get(render_state,
                             GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_HAS_VALUE,
                             &cursor_in_viewport);

    if (cursor_visible && cursor_in_viewport) {
        uint16_t cx = 0, cy = 0;
        ghostty_render_state_get(render_state,
                                 GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_X,
                                 &cx);
        ghostty_render_state_get(render_state,
                                 GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_Y,
                                 &cy);

        GhosttyColorRgb cur_rgb = colors.foreground;
        if (colors.cursor_has_value)
            cur_rgb = colors.cursor;
        int cur_x = grid_origin_x + cx * cell_width;
        int cur_y = grid_origin_y + cy * cell_height;
        DrawRectangle(cur_x, cur_y, cell_width, cell_height,
                      (Color){cur_rgb.r, cur_rgb.g, cur_rgb.b, 128});
    }

    if (scrollbar && scrollbar->total > scrollbar->len) {
        int scr_w = GetScreenWidth();
        int track_h = (int)term_rows * cell_height;
        if (track_h < 1)
            track_h = 1;

        const int bar_width = 6;
        const int bar_margin = 2;
        int bar_x = scr_w - pad_right - bar_width - bar_margin;

        double visible_frac = (double)scrollbar->len / (double)scrollbar->total;
        int thumb_height = (int)((double)track_h * visible_frac);
        if (thumb_height < 10)
            thumb_height = 10;
        if (thumb_height > track_h)
            thumb_height = track_h;

        double scroll_frac =
            (scrollbar->total > scrollbar->len)
                ? (double)scrollbar->offset /
                      (double)(scrollbar->total - scrollbar->len)
                : 1.0;
        int thumb_y =
            grid_origin_y +
            (int)(scroll_frac * (double)(track_h - thumb_height));

        DrawRectangle(bar_x, thumb_y, bar_width, thumb_height,
                      (Color){200, 200, 200, 128});
    }

    GhosttyRenderStateDirty clean_state = GHOSTTY_RENDER_STATE_DIRTY_FALSE;
    ghostty_render_state_set(render_state, GHOSTTY_RENDER_STATE_OPTION_DIRTY,
                             &clean_state);
}

void log_build_info(void)
{
    bool simd = false;
    ghostty_build_info(GHOSTTY_BUILD_INFO_SIMD, &simd);

    GhosttyOptimizeMode opt = GHOSTTY_OPTIMIZE_DEBUG;
    ghostty_build_info(GHOSTTY_BUILD_INFO_OPTIMIZE, &opt);

    const char *opt_str;
    switch (opt) {
    case GHOSTTY_OPTIMIZE_DEBUG:
        opt_str = "Debug";
        break;
    case GHOSTTY_OPTIMIZE_RELEASE_SAFE:
        opt_str = "ReleaseSafe";
        break;
    case GHOSTTY_OPTIMIZE_RELEASE_SMALL:
        opt_str = "ReleaseSmall";
        break;
    case GHOSTTY_OPTIMIZE_RELEASE_FAST:
        opt_str = "ReleaseFast";
        break;
    default:
        opt_str = "Unknown";
        break;
    }

    TraceLog(LOG_INFO, "ghostty-vt: simd:     %s", simd ? "enabled" : "disabled");
    TraceLog(LOG_INFO, "ghostty-vt: optimize: %s", opt_str);
}
