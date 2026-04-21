const std = @import("std");
const builtin = @import("builtin");

const c = @cImport({
    @cInclude("pty_common.h");
    @cInclude("util.h");
    @cInclude("sys/ioctl.h");
    @cInclude("unistd.h");
    @cInclude("stdlib.h");
    @cInclude("fcntl.h");
    @cInclude("errno.h");
});

comptime {
    if (builtin.os.tag == .windows) {
        @compileError("Windows not supported in pty.zig");
    }
}

const PtyHandle = c_int;
const PtyOutputSink = ?*const fn (userdata: ?*anyopaque, data: [*c]const u8, len: usize) callconv(.c) void;

export fn pty_write(fd: PtyHandle, buf: [*c]const u8, len: usize) void {
    var remaining = len;
    var ptr = @as([*c]const u8, @ptrCast(buf));
    while (remaining > 0) {
        const n = c.write(fd, ptr, remaining);
        if (n > 0) {
            ptr += @as(usize, @intCast(n));
            remaining -= @as(usize, @intCast(n));
        } else if (n < 0 and c.__error().* == c.EINTR) {
            continue;
        } else break;
    }
}

export fn pty_spawn_unix(
    child_out: *c_int,
    cols: u16,
    rows: u16,
    shell_override: [*c]const u8,
    cell_width: c_int,
    cell_height: c_int,
) c_int {
    var ws = std.mem.zeroes(c.struct_winsize);
    ws.ws_row = rows;
    ws.ws_col = cols;
    ws.ws_xpixel = @as(u16, @intCast(cols * @as(u32, @intCast(cell_width))));
    ws.ws_ypixel = @as(u16, @intCast(rows * @as(u32, @intCast(cell_height))));

    var pty_fd: c_int = undefined;
    const pid = c.forkpty(&pty_fd, null, null, &ws);
    if (pid < 0) return -1;
    if (pid == 0) {
        const shell = if (shell_override != null and shell_override[0] != 0)
            @as([*:0]const u8, @ptrCast(shell_override))
        else blk: {
            if (std.posix.getenv("SHELL")) |s| break :blk @as([*:0]const u8, @ptrCast(s.ptr));
            break :blk @as([*:0]const u8, @ptrCast("/bin/sh"));
        };
        const shell_name = std.fs.path.basename(std.mem.sliceTo(shell, 0));
        _ = c.setenv("TERM", "xterm-256color", 1);
        _ = c.execl(shell, shell_name.ptr, @as(?*anyopaque, null));
        c._exit(127);
    }
    child_out.* = pid;

    const flags = c.fcntl(pty_fd, c.F_GETFL, @as(c_int, 0));
    if (flags < 0) return pty_fd;
    _ = c.fcntl(pty_fd, c.F_SETFL, flags | c.O_NONBLOCK);
    return pty_fd;
}

export fn pty_read_unix(
    pty_fd: c_int,
    sink: PtyOutputSink,
    sink_userdata: ?*anyopaque,
) c_int {
    var buf: [4096]u8 = undefined;
    while (true) {
        const n = c.read(pty_fd, @as([*c]u8, @ptrCast(&buf)), buf.len);
        if (n > 0) {
            if (sink) |s| s(sink_userdata, @as([*c]const u8, @ptrCast(&buf)), @as(usize, @intCast(n)));
        } else if (n == 0) {
            return 1;
        } else {
            if (c.__error().* == c.EINTR) continue;
            if (c.__error().* == c.EAGAIN or c.__error().* == c.EWOULDBLOCK) return 0;
            return 2;
        }
    }
}

export fn pty_resize_unix(pty_fd: c_int, cols: u16, rows: u16) void {
    var ws = std.mem.zeroes(c.struct_winsize);
    ws.ws_row = rows;
    ws.ws_col = cols;
    _ = c.ioctl(pty_fd, c.TIOCSWINSZ, &ws);
}
