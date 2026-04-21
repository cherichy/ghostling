const std = @import("std");
const builtin = @import("builtin");

const c = @cImport({
    @cInclude("tabs.h");
    @cInclude("pty_common.h");
    @cInclude("pty_unix.h");
    @cInclude("effects.h");
    @cInclude("osc52_clipboard.h");
    @cInclude("agent_state.h");
    @cInclude("agent_events.h");
    @cInclude("ghostty/vt.h");
    @cInclude("raylib.h");
});

export fn tab_display_title(t: *const c.Tab, tab_index_one_based: usize, out: [*c]u8, outsz: usize) void {
    const e = &t.effects;
    if (e.title_override[0] != 0) {
        const len = std.mem.indexOfScalar(u8, &e.title_override, 0) orelse e.title_override.len;
        const n = @min(len, outsz - 1);
        @memcpy(out[0..n], e.title_override[0..n]);
        if (n > 0) out[n] = 0;
        return;
    }
    if (e.title_icon[0] != 0) {
        const len = std.mem.indexOfScalar(u8, &e.title_icon, 0) orelse e.title_icon.len;
        const n = @min(len, outsz - 1);
        @memcpy(out[0..n], e.title_icon[0..n]);
        if (n > 0) out[n] = 0;
        return;
    }
    if (e.title_shell[0] != 0) {
        const len = std.mem.indexOfScalar(u8, &e.title_shell, 0) orelse e.title_shell.len;
        const n = @min(len, outsz - 1);
        @memcpy(out[0..n], e.title_shell[0..n]);
        if (n > 0) out[n] = 0;
        return;
    }
    _ = tab_index_one_based;
    if (outsz > 0) out[0] = 0;
}

export fn tab_init_struct(t: *c.Tab) void {
    @memset(@as([*]u8, @ptrCast(t))[0..@sizeOf(c.Tab)], 0);
    c.ghostling_agent_state_init(&t.agent_state);
    c.osc52_clipboard_init(&t.osc52);
}

export fn tab_set_agent_state_hook(
    t: *c.Tab,
    hook: ?*const fn (?*anyopaque, ?*const anyopaque, ?*const c.GhostlingAgentState, ?*const c.GhostlingAgentState) callconv(.c) void,
    userdata: ?*anyopaque,
) void {
    t.agent_state_hook = @as(?*const fn (?*anyopaque, ?*const c.struct_Tab, ?*const c.GhostlingAgentState, ?*const c.GhostlingAgentState) callconv(.c) void, @ptrCast(hook));
    t.agent_state_hook_userdata = userdata;
}

export fn tab_agent_state_on_local_input(t: *c.Tab) void {
    c.ghostling_agent_state_on_local_input(&t.agent_state);
}

export fn tab_agent_state_on_process_exit(t: *c.Tab, exit_status: c_int) void {
    c.ghostling_agent_state_on_process_exit(&t.agent_state, exit_status);
}

export fn tab_free(t: *c.Tab) void {
    if (t.terminal) |term| {
        c.ghostty_terminal_free(term);
        t.terminal = null;
    }
    if (t.pty_fd >= 0) {
        std.posix.close(t.pty_fd);
        t.pty_fd = -1;
    }
    c.osc52_clipboard_deinit(&t.osc52);
    @memset(@as([*]u8, @ptrCast(t))[0..@sizeOf(c.Tab)], 0);
}

export fn tab_bind_ghostty_callbacks(t: *c.Tab) void {
    _ = c.ghostty_terminal_set(t.terminal, c.GHOSTTY_TERMINAL_OPT_USERDATA, &t.effects);
    _ = c.ghostty_terminal_set(t.terminal, c.GHOSTTY_TERMINAL_OPT_WRITE_PTY, @as(?*const anyopaque, @ptrCast(&c.effect_write_pty)));
    _ = c.ghostty_terminal_set(t.terminal, c.GHOSTTY_TERMINAL_OPT_SIZE, @as(?*const anyopaque, @ptrCast(&c.effect_size)));
    _ = c.ghostty_terminal_set(t.terminal, c.GHOSTTY_TERMINAL_OPT_DEVICE_ATTRIBUTES, @as(?*const anyopaque, @ptrCast(&c.effect_device_attributes)));
    _ = c.ghostty_terminal_set(t.terminal, c.GHOSTTY_TERMINAL_OPT_XTVERSION, @as(?*const anyopaque, @ptrCast(&c.effect_xtversion)));
    _ = c.ghostty_terminal_set(t.terminal, c.GHOSTTY_TERMINAL_OPT_TITLE_CHANGED, @as(?*const anyopaque, @ptrCast(&c.effect_title_changed)));
    _ = c.ghostty_terminal_set(t.terminal, c.GHOSTTY_TERMINAL_OPT_COLOR_SCHEME, @as(?*const anyopaque, @ptrCast(&c.effect_color_scheme)));
}

export fn tab_start_shell(
    t: *c.Tab,
    cols: u16,
    rows: u16,
    cell_width: c_int,
    cell_height: c_int,
    shell_override: [*c]const u8,
) bool {
    tab_init_struct(t);

    var opts = std.mem.zeroes(c.GhosttyTerminalOptions);
    opts.cols = cols;
    opts.rows = rows;
    opts.max_scrollback = 1000;
    const err = c.ghostty_terminal_new(null, &t.terminal, opts);
    if (err != c.GHOSTTY_SUCCESS) {
        tab_free(t);
        return false;
    }

    t.pty_fd = c.pty_spawn_unix(&t.child, cols, rows, shell_override, cell_width, cell_height);
    if (t.pty_fd < 0) {
        tab_free(t);
        return false;
    }

    t.effects.pty_fd = @as(c.PtyHandle, @intCast(t.pty_fd));
    t.effects.cell_width = cell_width;
    t.effects.cell_height = cell_height;
    t.effects.cols = cols;
    t.effects.rows = rows;

    tab_bind_ghostty_callbacks(t);

    t.child_exited = false;
    t.child_reaped = false;
    t.child_exit_status = -1;
    t.in_use = true;
    return true;
}

export fn tab_resize_pty(t: *c.Tab, cols: u16, rows: u16, cell_width: c_int, cell_height: c_int) void {
    if (!t.in_use) return;
    t.effects.cols = cols;
    t.effects.rows = rows;
    t.effects.cell_width = cell_width;
    t.effects.cell_height = cell_height;
    _ = c.ghostty_terminal_resize(t.terminal, cols, rows, @as(u32, @intCast(cell_width)), @as(u32, @intCast(cell_height)));
    c.pty_resize_unix(t.pty_fd, cols, rows);
}

fn tab_ingest_output(userdata: ?*anyopaque, data: [*c]const u8, len: usize) callconv(.c) void {
    const t = @as(*c.Tab, @ptrCast(@alignCast(userdata)));
    const slice = data[0..len];
    c.osc52_clipboard_scan(&t.osc52, data, len);
    c.ghostling_agent_state_feed_output(&t.agent_state, data, len);
    c.effect_scan_icon_osc(&t.effects, data, len);
    c.ghostty_terminal_vt_write(t.terminal, slice.ptr, slice.len);
}

export fn tab_drain(t: *c.Tab) c_int {
    if (!t.in_use or t.child_exited) return 0;
    return @as(c_int, @intCast(c.pty_read_unix(t.pty_fd, tab_ingest_output, @as(?*anyopaque, @ptrCast(t)))));
}

export fn tab_pty_write(t: *c.Tab) c.PtyHandle {
    return @as(c.PtyHandle, @intCast(t.pty_fd));
}
