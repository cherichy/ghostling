const std = @import("std");
const builtin = @import("builtin");

// This module provides Windows ConPTY support.
// On non-Windows platforms, it provides stub implementations.
// For full Windows support, the C implementation (pty_win.c) is still used.

pub const PtyOutputSink = *const fn (userdata: ?*anyopaque, data: [*c]const u8, len: usize) callconv(.c) void;

pub const PtyContext = struct {
    hpc: ?*anyopaque = null,
    process: ?*anyopaque = null,
    pipe_in: ?*anyopaque = null,
    pipe_out: ?*anyopaque = null,
};

pub const PTY_BUF_SIZE: usize = 65536;

pub const PtyReadBuf = struct {
    data: [PTY_BUF_SIZE]u8 = std.mem.zeroes([PTY_BUF_SIZE]u8),
    len: usize = 0,
    eof: bool = false,
    pipe: ?*anyopaque = null,
    cs: if (builtin.os.tag == .windows) std.os.windows.CRITICAL_SECTION else struct {} = .{},
};

pub const PtyReadResult = enum(c_int) {
    ok = 0,
    eof = 1,
    err = 2,
};

// Stub implementations for non-Windows platforms
// The actual Windows implementation uses pty_win.c

pub fn pty_spawn_win32(ctx: *PtyContext, cols: u16, rows: u16, shell_override: ?[*:0]const u8) bool {
    _ = ctx;
    _ = cols;
    _ = rows;
    _ = shell_override;
    if (builtin.os.tag == .windows) {
        @compileError("Windows ConPTY support requires pty_win.c - use the C implementation");
    }
    return false;
}

pub fn pty_reader_thread(param: ?*anyopaque) callconv(.c) c_int {
    _ = param;
    if (builtin.os.tag == .windows) {
        @compileError("Windows ConPTY support requires pty_win.c - use the C implementation");
    }
    return 0;
}

pub fn pty_buf_drain(rb: *PtyReadBuf, sink: ?PtyOutputSink, sink_userdata: ?*anyopaque) PtyReadResult {
    _ = rb;
    _ = sink;
    _ = sink_userdata;
    if (builtin.os.tag == .windows) {
        @compileError("Windows ConPTY support requires pty_win.c - use the C implementation");
    }
    return PtyReadResult.ok;
}

pub fn pty_resize_win32(hpc: ?*anyopaque, cols: u16, rows: u16) void {
    _ = hpc;
    _ = cols;
    _ = rows;
    if (builtin.os.tag == .windows) {
        @compileError("Windows ConPTY support requires pty_win.c - use the C implementation");
    }
}

pub fn pty_cleanup_win(ctx: *PtyContext) void {
    _ = ctx;
    if (builtin.os.tag == .windows) {
        @compileError("Windows ConPTY support requires pty_win.c - use the C implementation");
    }
}
