const std = @import("std");
const builtin = @import("builtin");

const c = @cImport({
    @cInclude("raylib.h");
    @cInclude("ghostty/vt.h");
    @cInclude("config_font.h");
    @cInclude("tabs.h");
    @cInclude("pty_common.h");
    @cInclude("terminal_ui.h");
    @cInclude("agent_events.h");
    @cInclude("agent_state.h");
    @cInclude("effects.h");
    @cInclude("font_jetbrains_mono.h");
    @cInclude("sys/stat.h");
    @cInclude("ghostty/vt/render.h");
});

const MaxTabs: usize = 16;
const TabStripWDefault: i32 = 156;
const TabEditNone: usize = std.math.maxInt(usize);

fn logBuildInfo() void {
    std.debug.print("ghostling (Zig) -- terminal emulator\n", .{});
}

fn fileMtimeSeconds(path: [*c]const u8, mtime: *c.time_t) bool {
    var st: c.struct_stat = undefined;
    if (c.stat(@as([*c]const u8, @ptrCast(path)), &st) == 0) {
        mtime.* = st.st_mtimespec.tv_sec;
        return true;
    }
    return false;
}

fn keyBindingTriggered(binding: *const c.GhostlingKeyBinding, state: *bool) bool {
    if (!binding.enabled) return false;
    if (binding.key != c.KEY_NULL and !c.IsKeyPressed(binding.key)) return false;

    const mods = binding.mods;
    const primary = (mods & @as(u8, @intCast(c.GHOSTLING_KEYMOD_PRIMARY))) != 0;
    const ctrl = (mods & @as(u8, @intCast(c.GHOSTLING_KEYMOD_CTRL))) != 0;
    const alt = (mods & @as(u8, @intCast(c.GHOSTLING_KEYMOD_ALT))) != 0;
    const shift = (mods & @as(u8, @intCast(c.GHOSTLING_KEYMOD_SHIFT))) != 0;
    const super = (mods & @as(u8, @intCast(c.GHOSTLING_KEYMOD_SUPER))) != 0;

    const primary_down = if (builtin.os.tag == .macos)
        c.IsKeyDown(c.KEY_LEFT_SUPER) or c.IsKeyDown(c.KEY_RIGHT_SUPER)
    else
        c.IsKeyDown(c.KEY_LEFT_CONTROL) or c.IsKeyDown(c.KEY_RIGHT_CONTROL);

    if (primary and !primary_down) return false;
    if (ctrl and !(c.IsKeyDown(c.KEY_LEFT_CONTROL) or c.IsKeyDown(c.KEY_RIGHT_CONTROL))) return false;
    if (alt and !(c.IsKeyDown(c.KEY_LEFT_ALT) or c.IsKeyDown(c.KEY_RIGHT_ALT))) return false;
    if (shift and !(c.IsKeyDown(c.KEY_LEFT_SHIFT) or c.IsKeyDown(c.KEY_RIGHT_SHIFT))) return false;
    if (super and !(c.IsKeyDown(c.KEY_LEFT_SUPER) or c.IsKeyDown(c.KEY_RIGHT_SUPER))) return false;

    if (!state.* and c.IsKeyPressed(binding.key)) {
        state.* = true;
        return true;
    }
    if (state.* and !c.IsKeyDown(binding.key)) {
        state.* = false;
    }
    return false;
}

pub fn main() void {
    logBuildInfo();
    std.debug.print("1: init raylib\n", .{});

    c.SetTraceLogCallback(raylibTraceFilter);
    c.SetConfigFlags(c.FLAG_WINDOW_HIGHDPI);
    c.InitWindow(800, 600, "ghostling");
    defer c.CloseWindow();
    c.SetExitKey(c.KEY_NULL);
    c.SetWindowState(c.FLAG_WINDOW_RESIZABLE);
    c.SetTargetFPS(60);
    std.debug.print("2: raylib ready\n", .{});

    const dpi = c.GetWindowScaleDPI();
    std.debug.print("3: dpi={d},{d}\n", .{ dpi.x, dpi.y });

    var app_cfg: c.AppConfig = undefined;
    c.config_load(&app_cfg);
    std.debug.print("4: config loaded, font_size={d}\n", .{app_cfg.font_size});
    const font_size = app_cfg.font_size;

    var cp_count: c_int = 0;
    std.debug.print("5: building codepoints...\n", .{});
    const codepoints = c.build_terminal_codepoints(&app_cfg.font_codepoint_set, &cp_count);
    std.debug.print("5b: codepoints={*} count={d}\n", .{ codepoints, cp_count });
    if (codepoints == null or cp_count <= 0) {
        std.debug.print("ghostling: out of memory building codepoint list\n", .{});
        return;
    }

    const font_size_px = @as(c_int, @intFromFloat(@as(f32, @floatFromInt(font_size)) * dpi.y));
    std.debug.print("6: font_size_px={d}\n", .{font_size_px});
    var mono_font: c.Font = undefined;
    {
        const embed: [*c]const u8 = @as([*c]const u8, &c.font_jetbrains_mono);
        const embed_size: c_int = @as(c_int, @intCast(@sizeOf(@TypeOf(c.font_jetbrains_mono))));
        std.debug.print("6b: loading font, embed_size={d}\n", .{embed_size});
        mono_font = c.load_terminal_font(&app_cfg.font_path, embed, embed_size, font_size_px, codepoints, cp_count);
        std.debug.print("6c: font glyphCount={d} texture.id={d}\n", .{ mono_font.glyphCount, mono_font.texture.id });
    }
    if (mono_font.glyphCount <= 0 or mono_font.texture.id <= 0) {
        std.debug.print("ghostling: failed to load font\n", .{});
        return;
    }
    if (mono_font.glyphCount <= 0 or mono_font.texture.id <= 0) {
        std.debug.print("ghostling: failed to load font\n", .{});
        return;
    }
    c.SetTextureFilter(mono_font.texture, c.TEXTURE_FILTER_POINT);

    const glyph = c.MeasureTextEx(mono_font, "M", @as(f32, @floatFromInt(font_size_px)), 0);
    var cell_width = @as(c_int, @intFromFloat((glyph.x / dpi.x) + 0.5));
    var cell_height = @as(c_int, @intFromFloat((glyph.y / dpi.y) + 0.5));
    if (cell_width < 1) cell_width = 1;
    if (cell_height < 1) cell_height = 1;

    const pad: c_int = 4;
    const tab_strip_w = TabStripWDefault;
    var tab_strip_collapsed = false;

    var tabs: [MaxTabs]c.Tab = undefined;
    var tab_ptrs: [MaxTabs]?*c.Tab = undefined;
    var n_tabs: usize = 0;
    var active: usize = 0;

    var scr_w = c.GetScreenWidth();
    var scr_h = c.GetScreenHeight();
    var term_cols: u16 = 1;
    var term_rows: u16 = 1;
    var grid_origin_x: c_int = tab_strip_w + pad;
    var grid_origin_y: c_int = pad;
    layoutTerminals(scr_w, scr_h, tab_strip_w, tab_strip_collapsed, cell_width, cell_height, pad, &term_cols, &term_rows, &grid_origin_x, &grid_origin_y);

    tab_ptrs[0] = &tabs[0];
    std.debug.print("7: starting shell...\n", .{});
    const shell_ok = c.tab_start_shell(&tabs[0], term_cols, term_rows, cell_width, cell_height, null);
    std.debug.print("7b: shell_ok={}\n", .{shell_ok});
    if (!shell_ok) {
        std.debug.print("ghostling: failed to start shell\n", .{});
        return;
    }
    n_tabs = 1;

    var event_bus: c.AgentEventBus = undefined;
    c.agent_event_bus_init(&event_bus);
    c.tab_set_agent_state_hook(&tabs[0], c.agent_event_bus_on_state_change, &event_bus);

    var key_new_tab_state = false;
    var key_close_tab_state = false;
    var key_next_tab_state = false;
    var key_prev_tab_state = false;
    var key_toggle_tab_strip_state = false;
    var key_reload_config_state = false;
    var loaded_config_mtime: c.time_t = 0;
    var has_loaded_config_mtime = false;
    if (app_cfg.loaded_config_path[0] != 0) {
        has_loaded_config_mtime = fileMtimeSeconds(&app_cfg.loaded_config_path, &loaded_config_mtime);
    }

    var render_state: c.GhosttyRenderState = null;
    var row_iter: c.GhosttyRenderStateRowIterator = null;
    var row_cells: c.GhosttyRenderStateRowCells = null;
    std.debug.print("8: creating render state...\n", .{});
    _ = c.ghostty_render_state_new(null, &render_state);
    _ = c.ghostty_render_state_row_iterator_new(null, &row_iter);
    _ = c.ghostty_render_state_row_cells_new(null, &row_cells);
    std.debug.print("8b: render_state={*} row_iter={*} row_cells={*}\n", .{ render_state, row_iter, row_cells });

    var last_tab_click_t: f64 = -100.0;
    _ = &last_tab_click_t;
    var last_tab_click_idx: usize = TabEditNone;
    _ = &last_tab_click_idx;
    var edit_tab: usize = TabEditNone;
    _ = &edit_tab;
    var edit_buf: [256]u8 = undefined;
    _ = &edit_buf;
    var splitter_dragging = false;
    _ = &splitter_dragging;
    var pending_config_reload = false;

    while (!c.WindowShouldClose()) {
        const cur = &tabs[active];
        _ = cur;

        if (pending_config_reload) {
            pending_config_reload = false;
            var new_cfg: c.AppConfig = undefined;
            c.config_load(&new_cfg);
            if (new_cfg.font_size != app_cfg.font_size or new_cfg.font_path[0] != 0) {
                _ = c.UnloadFont(mono_font);
                const new_font_size_px = @as(c_int, @intFromFloat(@as(f32, @floatFromInt(new_cfg.font_size)) * dpi.y));
                mono_font = c.load_terminal_font(&new_cfg.font_path, undefined, 0, new_font_size_px, codepoints, cp_count);
                c.SetTextureFilter(mono_font.texture, c.TEXTURE_FILTER_POINT);
                const new_glyph = c.MeasureTextEx(mono_font, "M", @as(f32, @floatFromInt(new_font_size_px)), 0);
                cell_width = @as(c_int, @intFromFloat((new_glyph.x / dpi.x) + 0.5));
                cell_height = @as(c_int, @intFromFloat((new_glyph.y / dpi.y) + 0.5));
                if (cell_width < 1) cell_width = 1;
                if (cell_height < 1) cell_height = 1;
            }
            app_cfg = new_cfg;
            for (0..n_tabs) |i| {
                c.tab_resize_pty(&tabs[i], term_cols, term_rows, cell_width, cell_height);
            }
        }

        if (has_loaded_config_mtime and app_cfg.loaded_config_path[0] != 0) {
            var current_mtime: c.time_t = 0;
            if (fileMtimeSeconds(&app_cfg.loaded_config_path, &current_mtime) and current_mtime != loaded_config_mtime) {
                loaded_config_mtime = current_mtime;
                pending_config_reload = true;
            }
        }

        for (0..n_tabs) |i| {
            _ = c.tab_drain(&tabs[i]);
        }

        const cur_tab = &tabs[active];

        if (keyBindingTriggered(&app_cfg.key_new_tab, &key_new_tab_state)) {
            if (n_tabs < MaxTabs) {
                tab_ptrs[n_tabs] = &tabs[n_tabs];
                if (c.tab_start_shell(&tabs[n_tabs], term_cols, term_rows, cell_width, cell_height, null)) {
                    c.tab_set_agent_state_hook(&tabs[n_tabs], c.agent_event_bus_on_state_change, &event_bus);
                    active = n_tabs;
                    n_tabs += 1;
                }
            }
        }

        if (keyBindingTriggered(&app_cfg.key_close_tab, &key_close_tab_state)) {
            if (n_tabs > 1) {
                c.tab_free(&tabs[active]);
                for (active..n_tabs - 1) |i| {
                    tabs[i] = tabs[i + 1];
                    tab_ptrs[i] = &tabs[i];
                }
                n_tabs -= 1;
                if (active >= n_tabs) active = n_tabs - 1;
            }
        }

        if (keyBindingTriggered(&app_cfg.key_next_tab, &key_next_tab_state)) {
            if (n_tabs > 1) active = (active + 1) % n_tabs;
        }

        if (keyBindingTriggered(&app_cfg.key_prev_tab, &key_prev_tab_state)) {
            if (n_tabs > 1) active = (active + n_tabs - 1) % n_tabs;
        }

        if (keyBindingTriggered(&app_cfg.key_toggle_tab_strip, &key_toggle_tab_strip_state)) {
            tab_strip_collapsed = !tab_strip_collapsed;
        }

        if (keyBindingTriggered(&app_cfg.key_reload_config, &key_reload_config_state)) {
            pending_config_reload = true;
        }

        scr_w = c.GetScreenWidth();
        scr_h = c.GetScreenHeight();
        layoutTerminals(scr_w, scr_h, tab_strip_w, tab_strip_collapsed, cell_width, cell_height, pad, &term_cols, &term_rows, &grid_origin_x, &grid_origin_y);

        const mpos = c.GetMousePosition();
        var mouse_handled = false;

        if (c.IsMouseButtonPressed(c.MOUSE_BUTTON_LEFT)) {
            if (tab_strip_w > 0 and !tab_strip_collapsed) {
                var hit_idx: usize = 0;
                var hit_act_uint: c_uint = 0;
                if (c.tab_strip_hit(mpos, tab_strip_w, scr_h, n_tabs, &hit_idx, &hit_act_uint, app_cfg.tab_title_h, app_cfg.tab_reserved_h, false)) {
                    const hit_act: c_int = @as(c_int, @intCast(hit_act_uint));
                    if (hit_act == 0) {
                        if (hit_idx < n_tabs) active = hit_idx;
                    } else if (hit_act == 1) {
                        if (n_tabs > 1) {
                            c.tab_free(&tabs[hit_idx]);
                            for (hit_idx..n_tabs - 1) |i| {
                                tabs[i] = tabs[i + 1];
                                tab_ptrs[i] = &tabs[i];
                            }
                            n_tabs -= 1;
                            if (active >= n_tabs) active = n_tabs - 1;
                        }
                    } else if (hit_act == 2) {
                        if (n_tabs < MaxTabs) {
                            tab_ptrs[n_tabs] = &tabs[n_tabs];
                            if (c.tab_start_shell(&tabs[n_tabs], term_cols, term_rows, cell_width, cell_height, null)) {
                                c.tab_set_agent_state_hook(&tabs[n_tabs], c.agent_event_bus_on_state_change, &event_bus);
                                active = n_tabs;
                                n_tabs += 1;
                            }
                        }
                    }
                    mouse_handled = true;
                }
            }
            if (!mouse_handled and c.tab_splitter_toggle_hit(mpos, tab_strip_w, scr_h)) {
                tab_strip_collapsed = !tab_strip_collapsed;
                mouse_handled = true;
            }
        }

        if (!mouse_handled) {
            _ = c.handle_mouse(cur_tab.pty_fd, null, null, cur_tab.terminal, cur_tab.effects.cell_width, cur_tab.effects.cell_height, grid_origin_x, grid_origin_y, 0, 0, scr_w, scr_h);
        }

        _ = c.handle_input(cur_tab.pty_fd, null, null, cur_tab.terminal);

        c.BeginDrawing();
        c.ClearBackground(c.BLACK);
        {
            _ = c.ghostty_render_state_update(render_state, cur_tab.terminal);
            var dirty: bool = false;
            _ = c.ghostty_render_state_get(render_state, c.GHOSTTY_RENDER_STATE_DATA_DIRTY, &dirty);
            var scrollbar: c.GhosttyTerminalScrollbar = undefined;
            _ = c.ghostty_terminal_get(cur_tab.terminal, c.GHOSTTY_TERMINAL_DATA_SCROLLBAR, &scrollbar);
            _ = c.render_terminal(render_state, row_iter, row_cells, mono_font, cur_tab.effects.cell_width, cur_tab.effects.cell_height, font_size_px, &scrollbar, grid_origin_x, grid_origin_y, cur_tab.effects.rows, 0, false, 0, 0, 0, 0, 0);
        }
        c.EndDrawing();
    }
}

fn layoutTerminals(
    scr_w: c_int,
    scr_h: c_int,
    tab_strip_w: c_int,
    tab_strip_collapsed: bool,
    cell_w: c_int,
    cell_h: c_int,
    pad: c_int,
    cols: *u16,
    rows: *u16,
    origin_x: *c_int,
    origin_y: *c_int,
) void {
    const effective_w = if (tab_strip_collapsed) @as(c_int, 0) else tab_strip_w;
    const avail_w = scr_w - effective_w - 2 * pad;
    const avail_h = scr_h - 2 * pad;
    if (cell_w > 0 and avail_w > 0) cols.* = @as(u16, @intCast(@max(1, @divTrunc(avail_w, cell_w))));
    if (cell_h > 0 and avail_h > 0) rows.* = @as(u16, @intCast(@max(1, @divTrunc(avail_h, cell_h))));
    origin_x.* = effective_w + pad;
    origin_y.* = pad;
}

fn raylibTraceFilter(logLevel: c_int, text: [*c]const u8, args: [*c]u8) callconv(.c) void {
    _ = args;
    _ = logLevel;
    _ = text;
}
