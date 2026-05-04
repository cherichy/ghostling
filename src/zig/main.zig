const std = @import("std");
const builtin = @import("builtin");

const c = @cImport({
    @cInclude("raylib.h");
    @cInclude("ghostty/vt.h");
    @cInclude("ghostty/vt/render.h");
    @cInclude("config_font.h");
    @cInclude("tabs.h");
    @cInclude("pty_common.h");
    @cInclude("terminal_ui.h");
    @cInclude("agent_events.h");
    @cInclude("agent_state.h");
    @cInclude("effects.h");
    @cInclude("font_jetbrains_mono.h");
    @cInclude("sys/stat.h");
    @cInclude("stdio.h");
    @cInclude("stdlib.h");
    @cInclude("string.h");
});

const MaxTabs: usize = 16;
const TabStripWDefault: c_int = 156;
const TabStripWMin: c_int = 64;
const TabEditNone: usize = std.math.maxInt(usize);

const FpsActive: c_int = 60;
const FpsIdle: c_int = 8;
const FpsUnfocused: c_int = 4;
const ActiveGraceSec: f64 = 0.30;

const KeyBindingState = struct {
    enabled: bool = false,
    pressed: bool = false,
};

fn utf8Encode(cp: u32, out: [*c]u8) c_int {
    const max_unicode: u32 = 0x10FFFF;
    const replacement: u32 = 0xFFFD;
    const cp2 = if (cp > max_unicode) replacement else cp;
    if (cp2 < 0x80) {
        out[0] = @as(u8, @intCast(cp2));
        return 1;
    }
    if (cp2 < 0x800) {
        out[0] = @as(u8, @intCast(0xC0 | (cp2 >> 6)));
        out[1] = @as(u8, @intCast(0x80 | (cp2 & 0x3F)));
        return 2;
    }
    if (cp2 < 0x10000) {
        out[0] = @as(u8, @intCast(0xE0 | (cp2 >> 12)));
        out[1] = @as(u8, @intCast(0x80 | ((cp2 >> 6) & 0x3F)));
        out[2] = @as(u8, @intCast(0x80 | (cp2 & 0x3F)));
        return 3;
    }
    out[0] = @as(u8, @intCast(0xF0 | (cp2 >> 18)));
    out[1] = @as(u8, @intCast(0x80 | ((cp2 >> 12) & 0x3F)));
    out[2] = @as(u8, @intCast(0x80 | ((cp2 >> 6) & 0x3F)));
    out[3] = @as(u8, @intCast(0x80 | (cp2 & 0x3F)));
    return 4;
}

fn utf8PopBack(s: [*c]u8) void {
    var n = c.strlen(s);
    if (n == 0) return;
    n -= 1;
    while (n > 0 and (s[n] & 0xC0) == 0x80) {
        n -= 1;
    }
    s[n] = 0;
}

fn fileMtimeSeconds(path: [*c]const u8, mtime: *c.time_t) bool {
    if (path == null or path[0] == 0) return false;
    var st: c.struct_stat = undefined;
    if (c.stat(path, &st) == 0) {
        mtime.* = st.st_mtimespec.tv_sec;
        return true;
    }
    return false;
}

fn keyBindingTriggered(binding: *const c.GhostlingKeyBinding, state: *KeyBindingState) bool {
    if (!binding.enabled) {
        state.pressed = false;
        state.enabled = false;
        return false;
    }

    const key_down = c.IsKeyDown(binding.key);
    const primary = (binding.mods & @as(u8, @intCast(c.GHOSTLING_KEYMOD_PRIMARY))) != 0;
    const ctrl = (binding.mods & @as(u8, @intCast(c.GHOSTLING_KEYMOD_CTRL))) != 0;
    const alt = (binding.mods & @as(u8, @intCast(c.GHOSTLING_KEYMOD_ALT))) != 0;
    const shift = (binding.mods & @as(u8, @intCast(c.GHOSTLING_KEYMOD_SHIFT))) != 0;
    const super_mod = (binding.mods & @as(u8, @intCast(c.GHOSTLING_KEYMOD_SUPER))) != 0;

    const primary_down = if (builtin.os.tag == .macos)
        c.IsKeyDown(c.KEY_LEFT_SUPER) or c.IsKeyDown(c.KEY_RIGHT_SUPER)
    else
        c.IsKeyDown(c.KEY_LEFT_CONTROL) or c.IsKeyDown(c.KEY_RIGHT_CONTROL);

    const ctrl_down = c.IsKeyDown(c.KEY_LEFT_CONTROL) or c.IsKeyDown(c.KEY_RIGHT_CONTROL);
    const alt_down = c.IsKeyDown(c.KEY_LEFT_ALT) or c.IsKeyDown(c.KEY_RIGHT_ALT);
    const shift_down = c.IsKeyDown(c.KEY_LEFT_SHIFT) or c.IsKeyDown(c.KEY_RIGHT_SHIFT);
    const super_down = c.IsKeyDown(c.KEY_LEFT_SUPER) or c.IsKeyDown(c.KEY_RIGHT_SUPER);

    const match = key_down and
        (!primary or primary_down) and
        (!ctrl or ctrl_down) and
        (!alt or alt_down) and
        (!shift or shift_down) and
        (!super_mod or super_down);

    const triggered = match and !state.pressed;
    state.pressed = match;
    state.enabled = true;
    return triggered;
}

fn shortcutPrimaryModifierDown() bool {
    if (builtin.os.tag == .macos) {
        return c.IsKeyDown(c.KEY_LEFT_SUPER) or c.IsKeyDown(c.KEY_RIGHT_SUPER);
    }
    return c.IsKeyDown(c.KEY_LEFT_CONTROL) or c.IsKeyDown(c.KEY_RIGHT_CONTROL);
}

fn selectionCopyShortcutPressed(shortcut: c.GhostlingCopyShortcut) bool {
    const primary = shortcutPrimaryModifierDown();
    const shift = c.IsKeyDown(c.KEY_LEFT_SHIFT) or c.IsKeyDown(c.KEY_RIGHT_SHIFT);
    if (!primary) return false;
    if (shortcut == c.GHOSTLING_COPY_SHORTCUT_CTRL_SHIFT_C)
        return shift and c.IsKeyPressed(c.KEY_C);
    return c.IsKeyPressed(c.KEY_C);
}

fn pasteShortcutPressed(shortcut: c.GhostlingPasteShortcut) bool {
    const primary = shortcutPrimaryModifierDown();
    const shift = c.IsKeyDown(c.KEY_LEFT_SHIFT) or c.IsKeyDown(c.KEY_RIGHT_SHIFT);
    if (!primary) return false;
    if (shortcut == c.GHOSTLING_PASTE_SHORTCUT_NONE) return false;
    if (shortcut == c.GHOSTLING_PASTE_SHORTCUT_CTRL_SHIFT_V)
        return shift and c.IsKeyPressed(c.KEY_V);
    return c.IsKeyPressed(c.KEY_V);
}

fn clampTabStripW(scr_w: c_int, cell_width: c_int, pad: c_int, strip_w: *c_int) void {
    const min_content_px = cell_width * 20;
    var max_strip = scr_w - pad * 2 - min_content_px;
    if (max_strip < TabStripWMin) max_strip = TabStripWMin;
    if (strip_w.* < TabStripWMin) strip_w.* = TabStripWMin;
    if (strip_w.* > max_strip) strip_w.* = max_strip;
}

fn tabStripLayoutW(collapsed: bool, expanded_w: c_int) c_int {
    return if (collapsed) @as(c_int, 0) else expanded_w;
}

fn layoutTerms(scr_w: c_int, scr_h: c_int, tab_strip_w: c_int, cell_width: c_int, cell_height: c_int, pad: c_int, term_cols: *u16, term_rows: *u16, grid_origin_x: *c_int, grid_origin_y: *c_int) void {
    grid_origin_x.* = tab_strip_w + pad;
    grid_origin_y.* = pad;
    const content_w = scr_w - grid_origin_x.* - pad;
    const content_h = scr_h - 2 * pad;
    var cols = @divTrunc(content_w, cell_width);
    var rows = @divTrunc(content_h, cell_height);
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    term_cols.* = @as(u16, @intCast(cols));
    term_rows.* = @as(u16, @intCast(rows));
}

fn mouseToCellClamped(mpos: c.Vector2, grid_origin_x: c_int, grid_origin_y: c_int, term_pixel_w: c_int, term_pixel_h: c_int, cell_width: c_int, cell_height: c_int, term_cols: u16, term_rows: u16, out_x: *u16, out_y: *u16) void {
    var x = mpos.x;
    var y = mpos.y;
    const max_x: f32 = @floatFromInt(grid_origin_x + term_pixel_w - 1);
    const max_y: f32 = @floatFromInt(grid_origin_y + term_pixel_h - 1);

    if (x < @as(f32, @floatFromInt(grid_origin_x))) x = @floatFromInt(grid_origin_x);
    if (y < @as(f32, @floatFromInt(grid_origin_y))) y = @floatFromInt(grid_origin_y);
    if (x > max_x) x = max_x;
    if (y > max_y) y = max_y;

    var col: c_int = @intFromFloat((x - @as(f32, @floatFromInt(grid_origin_x))) / @as(f32, @floatFromInt(cell_width)));
    var row: c_int = @intFromFloat((y - @as(f32, @floatFromInt(grid_origin_y))) / @as(f32, @floatFromInt(cell_height)));
    if (col < 0) col = 0;
    if (row < 0) row = 0;
    if (col >= @as(c_int, @intCast(term_cols))) col = @as(c_int, @intCast(term_cols)) - 1;
    if (row >= @as(c_int, @intCast(term_rows))) row = @as(c_int, @intCast(term_rows)) - 1;

    out_x.* = @as(u16, @intCast(col));
    out_y.* = @as(u16, @intCast(row));
}

fn tabSelectionClear(tab: *c.Tab) void {
    tab.selection_active = false;
    tab.selection_dragging = false;
}

fn tabSelectionNormalize(tab: *const c.Tab, x0: *u16, y0: *u16, x1: *u16, y1: *u16) void {
    x0.* = tab.selection_anchor_x;
    y0.* = tab.selection_anchor_y;
    x1.* = tab.selection_focus_x;
    y1.* = tab.selection_focus_y;

    if (y0.* > y1.* or (y0.* == y1.* and x0.* > x1.*)) {
        const tx = x0.*;
        const ty = y0.*;
        x0.* = x1.*;
        y0.* = y1.*;
        x1.* = tx;
        y1.* = ty;
    }
}

fn updateTerminalMetadata(tab: *c.Tab, window_title: [*c]u8, window_title_sz: usize) void {
    c.effect_sync_pwd(tab.terminal, &tab.effects);

    var tab_title: [256]u8 = std.mem.zeroes([256]u8);
    c.tab_display_title(tab, 1, @as([*c]u8, @ptrCast(&tab_title[0])), tab_title.len);
    if (tab_title[0] != 0) {
        _ = c.snprintf(window_title, window_title_sz, "%s", @as([*c]const u8, @ptrCast(&tab_title[0])));
        return;
    }

    if (tab.effects.pwd[0] != 0) {
        _ = c.snprintf(window_title, window_title_sz, "%s", @as([*c]const u8, @ptrCast(&tab.effects.pwd)));
        return;
    }

    _ = c.snprintf(window_title, window_title_sz, "%s", "ghostling");
}

fn reloadTerminalFont(font: *c.Font, cfg: *const c.AppConfig, dpi_scale: c.Vector2, font_size_px: *c_int, cell_width: *c_int, cell_height: *c_int) bool {
    var cp_count: c_int = 0;
    const codepoints = c.build_terminal_codepoints(&cfg.font_codepoint_set, &cp_count);
    if (codepoints == null or cp_count <= 0) {
        c.free(@as(?*anyopaque, @ptrCast(codepoints)));
        return false;
    }

    const new_font_size_px: c_int = @intFromFloat(@as(f32, @floatFromInt(cfg.font_size)) * dpi_scale.y);
    const font_path_try: [*c]const u8 = if (cfg.font_path[0] != 0) @as([*c]const u8, @ptrCast(&cfg.font_path)) else null;
    const new_font = c.load_terminal_font(font_path_try, @as([*c]const u8, @ptrCast(&c.font_jetbrains_mono)), @as(c_int, @intCast(@sizeOf(@TypeOf(c.font_jetbrains_mono)))), new_font_size_px, codepoints, cp_count);
    c.free(@as(?*anyopaque, @ptrCast(codepoints)));

    if (new_font.glyphCount <= 0 or new_font.texture.id == 0) return false;

    c.SetTextureFilter(new_font.texture, c.TEXTURE_FILTER_POINT);

    const glyph_size = c.MeasureTextEx(new_font, "M", @as(f32, @floatFromInt(new_font_size_px)), 0);
    var new_cell_w: c_int = @intFromFloat((glyph_size.x / dpi_scale.x) + 0.5);
    var new_cell_h: c_int = @intFromFloat((glyph_size.y / dpi_scale.y) + 0.5);
    if (new_cell_w < 1) new_cell_w = 1;
    if (new_cell_h < 1) new_cell_h = 1;

    c.UnloadFont(font.*);
    font.* = new_font;
    font_size_px.* = new_font_size_px;
    cell_width.* = new_cell_w;
    cell_height.* = new_cell_h;

    return true;
}

fn raylibTraceFilter(logLevel: c_int, text: [*c]const u8, args: [*c]u8) callconv(.c) void {
    if (logLevel == c.LOG_INFO and c.strstr(text, "TIMER: Target time per frame") != null) return;
    if (logLevel == c.LOG_WARNING and c.strstr(text, "FONT: Character [0x") != null and c.strstr(text, "size is bigger than expected font size") != null) return;

    const prefix = switch (logLevel) {
        c.LOG_TRACE => "TRACE",
        c.LOG_DEBUG => "DEBUG",
        c.LOG_INFO => "INFO",
        c.LOG_WARNING => "WARNING",
        c.LOG_ERROR => "ERROR",
        c.LOG_FATAL => "FATAL",
        else => "LOG",
    };

    const stderr = c.stderr();
    _ = c.fprintf(stderr, "%s: ", @as([*c]const u8, @ptrCast(prefix)));
    // vfprintf expects va_list which is a special type - use @ptrFromInt to force conversion
    const va_list: c.va_list = @ptrFromInt(@intFromPtr(args));
    _ = c.vfprintf(stderr, text, va_list);
    _ = c.fputc('\n', stderr);
}

pub fn main() void {
    c.SetTraceLogCallback(raylibTraceFilter);
    c.log_build_info();

    var app_cfg: c.AppConfig = undefined;
    c.config_load(&app_cfg);
    const font_size = app_cfg.font_size;

    c.SetConfigFlags(c.FLAG_WINDOW_HIGHDPI);
    c.InitWindow(800, 600, "ghostling");
    defer c.CloseWindow();
    c.SetExitKey(c.KEY_NULL);
    c.SetWindowState(c.FLAG_WINDOW_RESIZABLE);

    var target_fps: c_int = FpsActive;
    c.SetTargetFPS(target_fps);

    const dpi = c.GetWindowScaleDPI();

    var cp_count: c_int = 0;
    const codepoints = c.build_terminal_codepoints(&app_cfg.font_codepoint_set, &cp_count);
    if (codepoints == null or cp_count <= 0) {
        std.debug.print("ghostling: out of memory building codepoint list\n", .{});
        return;
    }

    var font_size_px: c_int = @intFromFloat(@as(f32, @floatFromInt(font_size)) * dpi.y);
    var mono_font = c.load_terminal_font(
        if (app_cfg.font_path[0] != 0) @as([*c]const u8, @ptrCast(&app_cfg.font_path)) else null,
        @as([*c]const u8, @ptrCast(&c.font_jetbrains_mono)),
        @as(c_int, @intCast(@sizeOf(@TypeOf(c.font_jetbrains_mono)))),
        font_size_px,
        codepoints,
        cp_count,
    );
    c.free(@as(?*anyopaque, @ptrCast(codepoints)));

    if (mono_font.glyphCount <= 0 or mono_font.texture.id <= 0) {
        std.debug.print("ghostling: failed to load font\n", .{});
        return;
    }
    c.SetTextureFilter(mono_font.texture, c.TEXTURE_FILTER_POINT);
    defer c.UnloadFont(mono_font);

    const glyph = c.MeasureTextEx(mono_font, "M", @as(f32, @floatFromInt(font_size_px)), 0);
    var cell_width: c_int = @intFromFloat((glyph.x / dpi.x) + 0.5);
    var cell_height: c_int = @intFromFloat((glyph.y / dpi.y) + 0.5);
    if (cell_width < 1) cell_width = 1;
    if (cell_height < 1) cell_height = 1;

    var current_han_tier = c.ghostling_han_tier_from_codepoint_set(&app_cfg.font_codepoint_set);

    const pad: c_int = 4;
    var tab_strip_w = TabStripWDefault;
    var tab_strip_collapsed = false;

    var tabs: [MaxTabs]c.Tab = undefined;
    var tab_ptrs: [MaxTabs]*c.Tab = undefined;
    var n_tabs: usize = 0;
    var active: usize = 0;

    var event_bus: c.AgentEventBus = undefined;
    c.agent_event_bus_init(&event_bus);

    var scr_w = c.GetScreenWidth();
    var scr_h = c.GetScreenHeight();
    var render_w = c.GetRenderWidth();
    var render_h = c.GetRenderHeight();
    var ui_w = scr_w;
    var ui_h = scr_h;
    if (render_w > 0 and dpi.x > 0.0) {
        const logical_w: c_int = @intFromFloat(@as(f32, @floatFromInt(render_w)) / dpi.x);
        if (logical_w > 0) ui_w = logical_w;
    }
    if (render_h > 0 and dpi.y > 0.0) {
        const logical_h: c_int = @intFromFloat(@as(f32, @floatFromInt(render_h)) / dpi.y);
        if (logical_h > 0) ui_h = logical_h;
    }

    var term_cols: u16 = 1;
    var term_rows: u16 = 1;
    var grid_origin_x: c_int = tab_strip_w + pad;
    var grid_origin_y: c_int = pad;
    clampTabStripW(ui_w, cell_width, pad, &tab_strip_w);
    layoutTerms(ui_w, ui_h, tabStripLayoutW(tab_strip_collapsed, tab_strip_w), cell_width, cell_height, pad, &term_cols, &term_rows, &grid_origin_x, &grid_origin_y);

    tabs[0] = std.mem.zeroes(c.Tab);
    if (!c.tab_start_shell(&tabs[0], term_cols, term_rows, cell_width, cell_height, null)) {
        std.debug.print("ghostling: failed to start shell\n", .{});
        return;
    }
    c.tab_set_agent_state_hook(&tabs[0], c.agent_event_bus_on_state_change, &event_bus);
    tab_ptrs[0] = &tabs[0];
    n_tabs = 1;

    var key_new_tab_state = KeyBindingState{};
    var key_close_tab_state = KeyBindingState{};
    var key_next_tab_state = KeyBindingState{};
    var key_prev_tab_state = KeyBindingState{};
    var key_toggle_tab_strip_state = KeyBindingState{};
    var key_reload_config_state = KeyBindingState{};
    var loaded_config_mtime: c.time_t = 0;
    var has_loaded_config_mtime = false;
    if (app_cfg.loaded_config_path[0] != 0) {
        has_loaded_config_mtime = fileMtimeSeconds(&app_cfg.loaded_config_path, &loaded_config_mtime);
    }

    var render_state: c.GhosttyRenderState = null;
    var row_iter: c.GhosttyRenderStateRowIterator = null;
    var row_cells: c.GhosttyRenderStateRowCells = null;
    var mouse_encoder: c.GhosttyMouseEncoder = null;
    var mouse_event: c.GhosttyMouseEvent = null;
    var key_encoder: c.GhosttyKeyEncoder = null;
    var key_event: c.GhosttyKeyEvent = null;
    _ = c.ghostty_render_state_new(null, &render_state);
    _ = c.ghostty_render_state_row_iterator_new(null, &row_iter);
    _ = c.ghostty_render_state_row_cells_new(null, &row_cells);
    _ = c.ghostty_mouse_encoder_new(null, &mouse_encoder);
    _ = c.ghostty_mouse_event_new(null, &mouse_event);
    _ = c.ghostty_key_encoder_new(null, &key_encoder);
    _ = c.ghostty_key_event_new(null, &key_event);

    var last_tab_click_t: f64 = -100.0;
    var last_tab_click_idx: usize = TabEditNone;
    var edit_tab: usize = TabEditNone;
    var edit_buf: [256]u8 = std.mem.zeroes([256]u8);
    var splitter_dragging = false;
    var scrollbar_dragging = false;
    var pending_config_reload = false;

    var prev_width = ui_w;
    var prev_height = ui_h;
    var prev_focused = c.IsWindowFocused();
    var last_activity_t = c.GetTime();
    var prev_mouse_pos = c.GetMousePosition();
    var window_title_cache: [280]u8 = std.mem.zeroes([280]u8);

    while (!c.WindowShouldClose()) {
        var frame_activity = false;
        scr_w = c.GetScreenWidth();
        scr_h = c.GetScreenHeight();
        render_w = c.GetRenderWidth();
        render_h = c.GetRenderHeight();

        // Check config file mtime for hot reload
        if (!pending_config_reload and app_cfg.loaded_config_path[0] != 0) {
            var now_mtime: c.time_t = 0;
            const has_now = fileMtimeSeconds(&app_cfg.loaded_config_path, &now_mtime);
            if (has_now and (!has_loaded_config_mtime or now_mtime != loaded_config_mtime)) {
                pending_config_reload = true;
                frame_activity = true;
            }
        }

        // Calculate UI dimensions from render size
        ui_w = scr_w;
        ui_h = scr_h;
        if (render_w > 0 and dpi.x > 0.0) {
            const logical_w: c_int = @intFromFloat(@as(f32, @floatFromInt(render_w)) / dpi.x);
            if (logical_w > 0) ui_w = logical_w;
        }
        if (render_h > 0 and dpi.y > 0.0) {
            const logical_h: c_int = @intFromFloat(@as(f32, @floatFromInt(render_h)) / dpi.y);
            if (logical_h > 0) ui_h = logical_h;
        }

        // Handle window resize
        if (ui_w != prev_width or ui_h != prev_height) {
            clampTabStripW(ui_w, cell_width, pad, &tab_strip_w);
            layoutTerms(ui_w, ui_h, tabStripLayoutW(tab_strip_collapsed, tab_strip_w), cell_width, cell_height, pad, &term_cols, &term_rows, &grid_origin_x, &grid_origin_y);
            for (0..n_tabs) |i| {
                c.tab_resize_pty(&tabs[i], term_cols, term_rows, cell_width, cell_height);
            }
            prev_width = ui_w;
            prev_height = ui_h;
            frame_activity = true;
        }

        // Handle splitter dragging
        if (splitter_dragging and !tab_strip_collapsed) {
            if (c.IsMouseButtonDown(c.MOUSE_BUTTON_LEFT)) {
                const nx: c_int = @intFromFloat(c.GetMousePosition().x);
                if (nx != tab_strip_w) {
                    tab_strip_w = nx;
                    clampTabStripW(ui_w, cell_width, pad, &tab_strip_w);
                    layoutTerms(ui_w, ui_h, tabStripLayoutW(tab_strip_collapsed, tab_strip_w), cell_width, cell_height, pad, &term_cols, &term_rows, &grid_origin_x, &grid_origin_y);
                    for (0..n_tabs) |i| {
                        c.tab_resize_pty(&tabs[i], term_cols, term_rows, cell_width, cell_height);
                    }
                    frame_activity = true;
                }
            } else {
                splitter_dragging = false;
            }
        }

        // Handle focus events
        const focused = c.IsWindowFocused();
        if (focused != prev_focused) {
            const cur = &tabs[active];
            var focus_mode = false;
            // Use raw value 1004 for GHOSTTY_MODE_FOCUS_EVENT to avoid cimport type issue
            const focus_mode_enum = c.ghostty_mode_new(1004, false);
            if (!cur.child_exited and
                c.ghostty_terminal_mode_get(cur.terminal, focus_mode_enum, &focus_mode) == c.GHOSTTY_SUCCESS and
                focus_mode)
            {
                const focus_event: c_uint = if (focused) c.GHOSTTY_FOCUS_GAINED else c.GHOSTTY_FOCUS_LOST;
                var focus_buf: [8]u8 = undefined;
                var focus_written: usize = 0;
                const focus_res = c.ghostty_focus_encode(@intCast(focus_event), @ptrCast(&focus_buf[0]), focus_buf.len, &focus_written);
                if (focus_res == c.GHOSTTY_SUCCESS and focus_written > 0) {
                    c.pty_write(cur.pty_fd, @ptrCast(&focus_buf[0]), focus_written);
                }
            }
            prev_focused = focused;
            frame_activity = true;
        }

        // Drain PTY output
        for (0..n_tabs) |i| {
            if (tabs[i].child_exited) continue;
            const pr = c.tab_drain(&tabs[i]);
            if (pr != c.PTY_READ_OK) {
                tabs[i].child_exited = true;
                frame_activity = true;
            }
        }

        // Reap child processes
        for (0..n_tabs) |i| {
            const t = &tabs[i];
            if (!t.child_exited or t.child_reaped) continue;
            var wstatus: c_int = 0;
            const wp = c.waitpid(t.child, &wstatus, c.WNOHANG);
            if (wp > 0) {
                t.child_reaped = true;
                if (c.WIFEXITED(wstatus)) {
                    t.child_exit_status = c.WEXITSTATUS(wstatus);
                } else if (c.WIFSIGNALED(wstatus)) {
                    t.child_exit_status = 128 + c.WTERMSIG(wstatus);
                }
                c.tab_agent_state_on_process_exit(t, t.child_exit_status);
            }
        }

        const cur = &tabs[active];

        // Handle key bindings
        if (keyBindingTriggered(&app_cfg.key_reload_config, &key_reload_config_state)) {
            pending_config_reload = true;
            frame_activity = true;
        }

        if (edit_tab == TabEditNone) {
            if (keyBindingTriggered(&app_cfg.key_toggle_tab_strip, &key_toggle_tab_strip_state)) {
                tab_strip_collapsed = !tab_strip_collapsed;
                clampTabStripW(ui_w, cell_width, pad, &tab_strip_w);
                layoutTerms(ui_w, ui_h, tabStripLayoutW(tab_strip_collapsed, tab_strip_w), cell_width, cell_height, pad, &term_cols, &term_rows, &grid_origin_x, &grid_origin_y);
                for (0..n_tabs) |i| {
                    c.tab_resize_pty(&tabs[i], term_cols, term_rows, cell_width, cell_height);
                }
                frame_activity = true;
            }

            if (keyBindingTriggered(&app_cfg.key_next_tab, &key_next_tab_state)) {
                if (n_tabs > 1) {
                    active = (active + 1) % n_tabs;
                    frame_activity = true;
                }
            }

            if (keyBindingTriggered(&app_cfg.key_prev_tab, &key_prev_tab_state)) {
                if (n_tabs > 1) {
                    active = (active + n_tabs - 1) % n_tabs;
                    frame_activity = true;
                }
            }

            if (keyBindingTriggered(&app_cfg.key_new_tab, &key_new_tab_state) and n_tabs < MaxTabs) {
                tabs[n_tabs] = std.mem.zeroes(c.Tab);
                if (c.tab_start_shell(&tabs[n_tabs], term_cols, term_rows, cell_width, cell_height, null)) {
                    c.tab_set_agent_state_hook(&tabs[n_tabs], c.agent_event_bus_on_state_change, &event_bus);
                    tab_ptrs[n_tabs] = &tabs[n_tabs];
                    active = n_tabs;
                    n_tabs += 1;
                    frame_activity = true;
                }
            }

            if (keyBindingTriggered(&app_cfg.key_close_tab, &key_close_tab_state) and n_tabs > 1) {
                c.tab_free(&tabs[active]);
                for (active..n_tabs - 1) |i| {
                    tabs[i] = tabs[i + 1];
                    tab_ptrs[i] = &tabs[i];
                }
                n_tabs -= 1;
                if (active >= n_tabs) active = n_tabs - 1;
                frame_activity = true;
            }
        }

        // Mouse handling
        const mpos = c.GetMousePosition();
        if (mpos.x != prev_mouse_pos.x or mpos.y != prev_mouse_pos.y) frame_activity = true;
        prev_mouse_pos = mpos;

        if (c.IsMouseButtonPressed(c.MOUSE_BUTTON_LEFT) or
            c.IsMouseButtonPressed(c.MOUSE_BUTTON_RIGHT) or
            c.IsMouseButtonPressed(c.MOUSE_BUTTON_MIDDLE) or
            c.IsMouseButtonReleased(c.MOUSE_BUTTON_LEFT) or
            c.IsMouseButtonReleased(c.MOUSE_BUTTON_RIGHT) or
            c.IsMouseButtonReleased(c.MOUSE_BUTTON_MIDDLE) or
            c.IsMouseButtonDown(c.MOUSE_BUTTON_LEFT) or
            c.IsMouseButtonDown(c.MOUSE_BUTTON_RIGHT) or
            c.IsMouseButtonDown(c.MOUSE_BUTTON_MIDDLE))
        {
            frame_activity = true;
        }

        var strip_w_eff = tabStripLayoutW(tab_strip_collapsed, tab_strip_w);

        // Set mouse cursor
        if (c.tab_splitter_toggle_hit(mpos, strip_w_eff, ui_h))
            c.SetMouseCursor(c.MOUSE_CURSOR_POINTING_HAND)
        else if (!tab_strip_collapsed and c.tab_splitter_hit(mpos, tab_strip_w, ui_h))
            c.SetMouseCursor(c.MOUSE_CURSOR_RESIZE_EW)
        else
            c.SetMouseCursor(c.MOUSE_CURSOR_DEFAULT);

        // Handle mouse clicks on tab strip
        if (c.IsMouseButtonPressed(c.MOUSE_BUTTON_LEFT)) {
            if (c.tab_splitter_toggle_hit(mpos, strip_w_eff, ui_h)) {
                tab_strip_collapsed = !tab_strip_collapsed;
                clampTabStripW(ui_w, cell_width, pad, &tab_strip_w);
                layoutTerms(ui_w, ui_h, tabStripLayoutW(tab_strip_collapsed, tab_strip_w), cell_width, cell_height, pad, &term_cols, &term_rows, &grid_origin_x, &grid_origin_y);
                for (0..n_tabs) |i| {
                    c.tab_resize_pty(&tabs[i], term_cols, term_rows, cell_width, cell_height);
                }
                splitter_dragging = false;
            } else if (!tab_strip_collapsed and c.tab_splitter_hit(mpos, tab_strip_w, ui_h)) {
                splitter_dragging = true;
                scrollbar_dragging = false;
            } else if (mpos.x < @as(f32, @floatFromInt(strip_w_eff)) and !splitter_dragging) {
                var hit_idx: usize = 0;
                var hit_act: c_uint = 0;
                if (c.tab_strip_hit(mpos, strip_w_eff, ui_h, n_tabs, &hit_idx, &hit_act, app_cfg.tab_title_h, app_cfg.tab_reserved_h, tab_strip_collapsed)) {
                    if (hit_act == 0) { // SELECT
                        const now = c.GetTime();
                        if (now - last_tab_click_t < 0.35 and hit_idx == last_tab_click_idx and hit_idx == active) {
                            edit_tab = hit_idx;
                            c.tab_display_title(&tabs[hit_idx], @as(u16, @intCast(hit_idx + 1)), @ptrCast(&edit_buf[0]), edit_buf.len);
                            last_tab_click_t = -100.0;
                        } else {
                            edit_tab = TabEditNone;
                            active = hit_idx;
                            last_tab_click_t = now;
                            last_tab_click_idx = hit_idx;
                        }
                        scrollbar_dragging = false;
                    } else if (hit_act == 1) { // CLOSE
                        if (n_tabs > 1) {
                            if (edit_tab == hit_idx) edit_tab = TabEditNone else if (edit_tab != TabEditNone and edit_tab > hit_idx) edit_tab -= 1;
                            c.tab_free(&tabs[hit_idx]);
                            for (hit_idx..n_tabs - 1) |i| {
                                tabs[i] = tabs[i + 1];
                                tab_ptrs[i] = &tabs[i];
                            }
                            n_tabs -= 1;
                            if (active >= n_tabs) active = n_tabs - 1;
                            scrollbar_dragging = false;
                        }
                    } else if (hit_act == 2) { // NEW
                        if (n_tabs < MaxTabs) {
                            edit_tab = TabEditNone;
                            tabs[n_tabs] = std.mem.zeroes(c.Tab);
                            if (c.tab_start_shell(&tabs[n_tabs], term_cols, term_rows, cell_width, cell_height, null)) {
                                c.tab_set_agent_state_hook(&tabs[n_tabs], c.agent_event_bus_on_state_change, &event_bus);
                                tab_ptrs[n_tabs] = &tabs[n_tabs];
                                active = n_tabs;
                                n_tabs += 1;
                                scrollbar_dragging = false;
                            }
                        }
                    }
                }
            }
        }

        strip_w_eff = tabStripLayoutW(tab_strip_collapsed, tab_strip_w);

        // Close edit mode when clicking in terminal area
        if (edit_tab != TabEditNone and c.IsMouseButtonPressed(c.MOUSE_BUTTON_LEFT) and mpos.x >= @as(f32, @floatFromInt(grid_origin_x))) {
            edit_tab = TabEditNone;
        }

        // Handle tab name editing
        if (edit_tab != TabEditNone) {
            if (c.IsKeyPressed(c.KEY_ESCAPE)) {
                edit_tab = TabEditNone;
                frame_activity = true;
            } else if (c.IsKeyPressed(c.KEY_ENTER)) {
                _ = c.snprintf(&tabs[edit_tab].effects.title_override, tabs[edit_tab].effects.title_override.len, "%s", @as([*c]const u8, @ptrCast(&edit_buf[0])));
                edit_tab = TabEditNone;
                frame_activity = true;
            } else {
                if (c.IsKeyPressed(c.KEY_BACKSPACE) or c.IsKeyPressedRepeat(c.KEY_BACKSPACE)) {
                    utf8PopBack(@ptrCast(&edit_buf[0]));
                    frame_activity = true;
                }
                while (true) {
                    const ch = c.GetCharPressed();
                    if (ch == 0) break;
                    const len = c.strlen(@ptrCast(&edit_buf[0]));
                    if (len + 4 >= edit_buf.len) break;
                    var u8_buf: [4]u8 = undefined;
                    const n = utf8Encode(@as(u32, @intCast(ch)), @as([*c]u8, @ptrCast(&u8_buf[0])));
                    if (len + @as(usize, @intCast(n)) < edit_buf.len) {
                        @memcpy(edit_buf[len .. len + @as(usize, @intCast(n))], u8_buf[0..@as(usize, @intCast(n))]);
                        edit_buf[len + @as(usize, @intCast(n))] = 0;
                        frame_activity = true;
                    }
                }
            }
        }

        // Handle scrollbar
        const scrollbar_consumed = c.handle_scrollbar(cur.terminal, render_state, &scrollbar_dragging, grid_origin_x, grid_origin_y, term_rows, cell_height, pad);
        if (scrollbar_consumed or scrollbar_dragging) frame_activity = true;

        // Handle terminal input and mouse
        if (!cur.child_exited and edit_tab == TabEditNone) {
            const term_pixel_w: c_int = @as(c_int, @intCast(term_cols)) * cell_width;
            const term_pixel_h: c_int = @as(c_int, @intCast(term_rows)) * cell_height;
            const mouse_in_terminal = mpos.x >= @as(f32, @floatFromInt(grid_origin_x)) and
                mpos.y >= @as(f32, @floatFromInt(grid_origin_y)) and
                mpos.x < @as(f32, @floatFromInt(grid_origin_x + term_pixel_w)) and
                mpos.y < @as(f32, @floatFromInt(grid_origin_y + term_pixel_h));

            var mouse_tracking = false;
            _ = c.ghostty_terminal_get(cur.terminal, c.GHOSTTY_TERMINAL_DATA_MOUSE_TRACKING, &mouse_tracking);

            var hyperlink_clicked = false;

            if (mouse_in_terminal) {
                var hover_x: u16 = 0;
                var hover_y: u16 = 0;
                mouseToCellClamped(mpos, grid_origin_x, grid_origin_y, term_pixel_w, term_pixel_h, cell_width, cell_height, term_cols, term_rows, &hover_x, &hover_y);
                if (c.cell_has_hyperlink(cur.terminal, hover_x, hover_y))
                    c.SetMouseCursor(c.MOUSE_CURSOR_POINTING_HAND);

                if (c.IsMouseButtonPressed(c.MOUSE_BUTTON_LEFT) and shortcutPrimaryModifierDown()) {
                    if (c.open_url_at_cell(cur.terminal, term_cols, term_rows, hover_x, hover_y)) {
                        hyperlink_clicked = true;
                        frame_activity = true;
                    }
                }
            }

            // Text selection
            if (!hyperlink_clicked and !mouse_tracking and !scrollbar_consumed) {
                if (c.IsMouseButtonPressed(c.MOUSE_BUTTON_LEFT)) {
                    if (mouse_in_terminal and !(tab_strip_collapsed and c.tab_splitter_toggle_hit(mpos, 0, ui_h))) {
                        var sel_x: u16 = 0;
                        var sel_y: u16 = 0;
                        mouseToCellClamped(mpos, grid_origin_x, grid_origin_y, term_pixel_w, term_pixel_h, cell_width, cell_height, term_cols, term_rows, &sel_x, &sel_y);
                        cur.selection_anchor_x = sel_x;
                        cur.selection_anchor_y = sel_y;
                        cur.selection_focus_x = sel_x;
                        cur.selection_focus_y = sel_y;
                        cur.selection_active = true;
                        cur.selection_dragging = true;
                        frame_activity = true;
                    } else if (cur.selection_active) {
                        tabSelectionClear(cur);
                        frame_activity = true;
                    }
                }

                if (cur.selection_dragging and c.IsMouseButtonDown(c.MOUSE_BUTTON_LEFT)) {
                    var sel_x = cur.selection_focus_x;
                    var sel_y = cur.selection_focus_y;
                    mouseToCellClamped(mpos, grid_origin_x, grid_origin_y, term_pixel_w, term_pixel_h, cell_width, cell_height, term_cols, term_rows, &sel_x, &sel_y);
                    if (sel_x != cur.selection_focus_x or sel_y != cur.selection_focus_y) {
                        cur.selection_focus_x = sel_x;
                        cur.selection_focus_y = sel_y;
                        frame_activity = true;
                    }
                }

                if (cur.selection_dragging and c.IsMouseButtonReleased(c.MOUSE_BUTTON_LEFT)) {
                    cur.selection_dragging = false;
                    frame_activity = true;

                    if (app_cfg.selection_copy_on_select and cur.selection_active) {
                        var x0: u16 = 0;
                        var y0: u16 = 0;
                        var x1: u16 = 0;
                        var y1: u16 = 0;
                        tabSelectionNormalize(cur, &x0, &y0, &x1, &y1);
                        if (c.copy_viewport_selection_to_clipboard(cur.terminal, term_cols, term_rows, x0, y0, x1, y1))
                            frame_activity = true;
                    }
                }
            }

            // Copy shortcut
            const copy_triggered = selectionCopyShortcutPressed(app_cfg.selection_copy_shortcut) and cur.selection_active;
            const copied_shortcut = copy_triggered;
            if (copy_triggered) {
                var x0: u16 = 0;
                var y0: u16 = 0;
                var x1: u16 = 0;
                var y1: u16 = 0;
                tabSelectionNormalize(cur, &x0, &y0, &x1, &y1);
                if (c.copy_viewport_selection_to_clipboard(cur.terminal, term_cols, term_rows, x0, y0, x1, y1)) {
                    tabSelectionClear(cur);
                    frame_activity = true;
                }
            }

            // Paste shortcut
            const paste_triggered = pasteShortcutPressed(app_cfg.paste_shortcut);
            const pasted_shortcut = paste_triggered;
            if (paste_triggered) {
                if (c.paste_host_clipboard_to_terminal(cur.pty_fd, cur.terminal)) {
                    c.tab_agent_state_on_local_input(cur);
                    tabSelectionClear(cur);
                    frame_activity = true;
                }
            }

            // Handle keyboard input
            if (!copied_shortcut and !pasted_shortcut and c.handle_input(cur.pty_fd, key_encoder, key_event, cur.terminal)) {
                c.tab_agent_state_on_local_input(cur);
                frame_activity = true;
            }

            // Handle mouse input
            const mouse_pad_right = ui_w - grid_origin_x - term_pixel_w;
            const mouse_pad_bottom = ui_h - grid_origin_y - term_pixel_h;
            if (focused and !hyperlink_clicked and !scrollbar_consumed and mouse_in_terminal and
                !(tab_strip_collapsed and c.tab_splitter_toggle_hit(mpos, 0, ui_h)))
            {
                if (c.handle_mouse(cur.pty_fd, mouse_encoder, mouse_event, cur.terminal, cell_width, cell_height, grid_origin_x, grid_origin_y, @max(0, mouse_pad_right), @max(0, mouse_pad_bottom), ui_w, ui_h))
                    frame_activity = true;
            }
        }

        // Update render state
        _ = c.ghostty_render_state_update(render_state, cur.terminal);
        var render_dirty: c.GhosttyRenderStateDirty = c.GHOSTTY_RENDER_STATE_DIRTY_FALSE;
        if (c.ghostty_render_state_get(render_state, c.GHOSTTY_RENDER_STATE_DATA_DIRTY, &render_dirty) == c.GHOSTTY_SUCCESS and
            render_dirty != c.GHOSTTY_RENDER_STATE_DIRTY_FALSE)
        {
            frame_activity = true;
        }

        // Update window title
        var wtitle: [280]u8 = std.mem.zeroes([280]u8);
        updateTerminalMetadata(cur, @ptrCast(&wtitle[0]), wtitle.len);
        if (c.strcmp(@ptrCast(&window_title_cache[0]), @ptrCast(&wtitle[0])) != 0) {
            c.SetWindowTitle(@ptrCast(&wtitle[0]));
            @memcpy(&window_title_cache, &wtitle);
            frame_activity = true;
        }

        // Dynamic FPS management
        const now_t = c.GetTime();
        if (frame_activity) last_activity_t = now_t;
        const active_recent = (now_t - last_activity_t) < ActiveGraceSec;
        const desired_fps: c_int = if (focused) (if (active_recent) FpsActive else FpsIdle) else (if (active_recent) FpsIdle else FpsUnfocused);
        if (desired_fps != target_fps) {
            c.SetTargetFPS(desired_fps);
            target_fps = desired_fps;
        }

        // Get background color
        var bg_colors = std.mem.zeroes(c.GhosttyRenderStateColors);
        bg_colors.size = @sizeOf(c.GhosttyRenderStateColors);
        _ = c.ghostty_render_state_colors_get(render_state, &bg_colors);
        const win_bg = c.Color{ .r = bg_colors.background.r, .g = bg_colors.background.g, .b = bg_colors.background.b, .a = 255 };

        // Get scrollbar
        var scrollbar = std.mem.zeroes(c.GhosttyTerminalScrollbar);
        var scrollbar_ptr: ?*c.GhosttyTerminalScrollbar = null;
        if (c.ghostty_terminal_get(cur.terminal, c.GHOSTTY_TERMINAL_DATA_SCROLLBAR, &scrollbar) == c.GHOSTTY_SUCCESS)
            scrollbar_ptr = &scrollbar;

        // Tab strip colors
        const strip_bg = c.Color{ .r = 45, .g = 45, .b = 48, .a = 255 };
        const tab_index_bg = c.Color{ .r = 40, .g = 40, .b = 44, .a = 255 };
        const tab_reserved_bg = c.Color{ .r = 38, .g = 38, .b = 42, .a = 255 };
        const tab_bg = c.Color{ .r = 55, .g = 55, .b = 58, .a = 255 };
        const tab_active_color = c.Color{ .r = 70, .g = 100, .b = 140, .a = 255 };
        const border = c.Color{ .r = 80, .g = 80, .b = 85, .a = 255 };
        const tab_fg = c.Color{ .r = 220, .g = 220, .b = 220, .a = 255 };
        const edit_bg = c.Color{ .r = 50, .g = 70, .b = 95, .a = 255 };

        // Get selection state
        const selection_active = cur.selection_active;
        var sel_x0: u16 = 0;
        var sel_y0: u16 = 0;
        var sel_x1: u16 = 0;
        var sel_y1: u16 = 0;
        if (selection_active) tabSelectionNormalize(cur, &sel_x0, &sel_y0, &sel_x1, &sel_y1);

        // Render
        c.BeginDrawing();
        c.ClearBackground(win_bg);
        const missing_han_tier = c.render_terminal(
            render_state,
            row_iter,
            row_cells,
            mono_font,
            cell_width,
            cell_height,
            font_size,
            scrollbar_ptr,
            grid_origin_x,
            grid_origin_y,
            term_rows,
            pad,
            selection_active,
            sel_x0,
            sel_y0,
            sel_x1,
            sel_y1,
            current_han_tier,
        );

        // Draw tab strip
        const tab_title_font_px: f32 = @as(f32, @floatFromInt(font_size)) * app_cfg.tab_title_font_scale;
        c.tab_strip_draw(
            &mono_font,
            if (tab_title_font_px < 6.0) 6.0 else tab_title_font_px,
            tabStripLayoutW(tab_strip_collapsed, tab_strip_w),
            ui_h,
            @ptrCast(&tab_ptrs[0]),
            n_tabs,
            active,
            edit_tab,
            if (edit_tab != TabEditNone) @as([*c]const u8, @ptrCast(&edit_buf)) else null,
            strip_bg,
            tab_index_bg,
            tab_reserved_bg,
            tab_bg,
            tab_active_color,
            border,
            tab_fg,
            edit_bg,
            app_cfg.tab_title_h,
            app_cfg.tab_reserved_h,
            tab_strip_collapsed,
        );

        // Draw splitter highlight
        if (!tab_strip_collapsed and (splitter_dragging or c.tab_splitter_hit(mpos, tab_strip_w, ui_h))) {
            const sx = tab_strip_w - 1;
            c.DrawRectangle(sx, 0, 2, ui_h, .{ .r = 120, .g = 160, .b = 220, .a = 255 });
        }

        // Draw splitter toggle
        c.tab_splitter_toggle_draw(
            &mono_font,
            if (tab_title_font_px < 6.0) 6.0 else tab_title_font_px,
            tabStripLayoutW(tab_strip_collapsed, tab_strip_w),
            ui_h,
            tab_strip_collapsed,
            c.tab_splitter_toggle_hit(mpos, if (tab_strip_collapsed) @as(c_int, 0) else tab_strip_w, ui_h),
            tab_fg,
        );

        // Draw exit message banner
        if (cur.child_exited) {
            var exit_msg: [128]u8 = undefined;
            if (cur.child_exit_status >= 0)
                _ = c.snprintf(@ptrCast(&exit_msg[0]), exit_msg.len, "[process exited with status %d]", cur.child_exit_status)
            else
                _ = c.snprintf(@ptrCast(&exit_msg[0]), exit_msg.len, "[process exited]");

            const msg_size = c.MeasureTextEx(mono_font, @ptrCast(&exit_msg[0]), @as(f32, @floatFromInt(font_size)), 0);
            const screen_w = c.GetScreenWidth();
            const screen_h = c.GetScreenHeight();
            const banner_h: c_int = @intFromFloat(msg_size.y + 8);
            c.DrawRectangle(0, screen_h - banner_h, screen_w, banner_h, .{ .r = 0, .g = 0, .b = 0, .a = 180 });
            c.DrawTextEx(mono_font, @ptrCast(&exit_msg[0]), .{ .x = @as(f32, @floatFromInt(screen_w)) - msg_size.x / 2.0, .y = @as(f32, @floatFromInt(screen_h - banner_h + 4)) }, @as(f32, @floatFromInt(font_size)), 0, c.WHITE);
        }

        c.EndDrawing();

        // Handle config reload
        if (pending_config_reload) {
            var loaded: c.AppConfig = undefined;
            c.config_load_profile(&loaded, &app_cfg.profile);

            const font_set_changed = c.strcmp(@as([*c]const u8, @ptrCast(&loaded.font_codepoint_set)), @as([*c]const u8, @ptrCast(&app_cfg.font_codepoint_set))) != 0;
            const font_path_changed = c.strcmp(@as([*c]const u8, @ptrCast(&loaded.font_path)), @as([*c]const u8, @ptrCast(&app_cfg.font_path))) != 0;
            const font_size_changed = loaded.font_size != app_cfg.font_size;

            app_cfg = loaded;
            has_loaded_config_mtime = fileMtimeSeconds(&app_cfg.loaded_config_path, &loaded_config_mtime);

            if (font_set_changed or font_path_changed or font_size_changed) {
                if (reloadTerminalFont(&mono_font, &app_cfg, dpi, &font_size_px, &cell_width, &cell_height)) {
                    current_han_tier = c.ghostling_han_tier_from_codepoint_set(&app_cfg.font_codepoint_set);
                    clampTabStripW(ui_w, cell_width, pad, &tab_strip_w);
                    layoutTerms(ui_w, ui_h, tabStripLayoutW(tab_strip_collapsed, tab_strip_w), cell_width, cell_height, pad, &term_cols, &term_rows, &grid_origin_x, &grid_origin_y);
                    for (0..n_tabs) |i| {
                        c.tab_resize_pty(&tabs[i], term_cols, term_rows, cell_width, cell_height);
                    }
                }
            }

            pending_config_reload = false;
        }

        // Auto-upgrade Han tier on glyph miss
        if (current_han_tier != c.GHOSTLING_HAN_TIER_NONE and missing_han_tier > current_han_tier) {
            var next_han_tier = current_han_tier;
            if (current_han_tier == c.GHOSTLING_HAN_TIER_3500)
                next_han_tier = c.GHOSTLING_HAN_TIER_6500
            else if (current_han_tier == c.GHOSTLING_HAN_TIER_6500)
                next_han_tier = c.GHOSTLING_HAN_TIER_8105;

            const next_set = c.ghostling_codepoint_set_for_han_tier(next_han_tier);
            if (next_set != null) {
                _ = c.snprintf(&app_cfg.font_codepoint_set, app_cfg.font_codepoint_set.len, "%s", next_set);

                if (reloadTerminalFont(&mono_font, &app_cfg, dpi, &font_size_px, &cell_width, &cell_height)) {
                    current_han_tier = next_han_tier;
                    frame_activity = true;
                    clampTabStripW(ui_w, cell_width, pad, &tab_strip_w);
                    layoutTerms(ui_w, ui_h, tabStripLayoutW(tab_strip_collapsed, tab_strip_w), cell_width, cell_height, pad, &term_cols, &term_rows, &grid_origin_x, &grid_origin_y);
                    for (0..n_tabs) |i| {
                        c.tab_resize_pty(&tabs[i], term_cols, term_rows, cell_width, cell_height);
                    }
                    _ = c.fprintf(c.stderr(), "ghostling: upgraded Han table tier to %s after glyph miss\n", next_set);
                }
            }
        }
    }

    // Cleanup
    for (0..n_tabs) |i| {
        c.tab_free(&tabs[i]);
    }

    if (mouse_event != null) c.ghostty_mouse_event_free(mouse_event);
    if (mouse_encoder != null) c.ghostty_mouse_encoder_free(mouse_encoder);
    if (key_event != null) c.ghostty_key_event_free(key_event);
    if (key_encoder != null) c.ghostty_key_encoder_free(key_encoder);
    if (row_cells != null) c.ghostty_render_state_row_cells_free(row_cells);
    if (row_iter != null) c.ghostty_render_state_row_iterator_free(row_iter);
    if (render_state != null) c.ghostty_render_state_free(render_state);
}
