const std = @import("std");

const rl = @cImport({
    @cInclude("raylib.h");
});

pub const State = extern struct {
    mode: c_int = 0,
    osc_esc_pending: bool = false,
    osc_cmd_decided: bool = false,
    osc_collect: bool = true,
    osc_cmd: c_uint = 0,
    osc_cmd_digits: usize = 0,
    osc_buf: ?[*]u8 = null,
    osc_len: usize = 0,
    osc_cap: usize = 0,
};

const max_command_bytes: usize = 1024 * 1024;

export fn osc52_clipboard_init(s: *State) void {
    s.* = State{};
}

export fn osc52_clipboard_deinit(s: *State) void {
    if (s.osc_buf) |ptr| {
        const alloc = std.heap.c_allocator;
        alloc.free(ptr[0..s.osc_cap]);
    }
    s.* = State{};
}

fn resetCommand(s: *State) void {
    s.osc_esc_pending = false;
    s.osc_cmd_decided = false;
    s.osc_collect = true;
    s.osc_cmd = 0;
    s.osc_cmd_digits = 0;
    s.osc_len = 0;
}

fn appendByte(s: *State, b: u8) bool {
    if (s.osc_len >= max_command_bytes) return false;
    if (s.osc_len == s.osc_cap) {
        const new_cap = if (s.osc_cap == 0) 256 else @min(s.osc_cap * 2, max_command_bytes);
        if (new_cap <= s.osc_cap) return false;
        const alloc = std.heap.c_allocator;
        if (s.osc_buf) |ptr| {
            const old_slice = ptr[0..s.osc_cap];
            const new_buf = alloc.realloc(old_slice, new_cap) catch return false;
            s.osc_buf = new_buf.ptr;
        } else {
            const new_buf = alloc.alloc(u8, new_cap) catch return false;
            s.osc_buf = new_buf.ptr;
        }
        s.osc_cap = new_cap;
    }
    const buf = s.osc_buf orelse return false;
    buf[s.osc_len] = b;
    s.osc_len += 1;
    return true;
}

fn base64Value(c: u8) i32 {
    return switch (c) {
        'A'...'Z' => @as(i32, @intCast(c - 'A')),
        'a'...'z' => @as(i32, @intCast(c - 'a' + 26)),
        '0'...'9' => @as(i32, @intCast(c - '0' + 52)),
        '+' => 62,
        '/' => 63,
        else => -1,
    };
}

fn base64Decode(in_data: []const u8) ![]u8 {
    const alloc = std.heap.c_allocator;
    var buf = try alloc.alloc(u8, (in_data.len / 4 + 1) * 3 + 1);
    var n: usize = 0;
    var acc: u32 = 0;
    var bits: i32 = 0;

    for (in_data) |c| {
        if (c == '=') break;
        const v = base64Value(c);
        if (v < 0) {
            if (c == ' ' or c == '\t' or c == '\r' or c == '\n') continue;
            alloc.free(buf);
            return error.InvalidBase64;
        }
        acc = (acc << 6) | @as(u32, @intCast(v));
        bits += 6;
        while (bits >= 8) {
            bits -= 8;
            if (n + 1 >= buf.len) {
                buf = try alloc.realloc(buf, buf.len * 2);
            }
            buf[n] = @as(u8, @intCast((acc >> @as(u5, @intCast(bits))) & 0xFF));
            n += 1;
        }
    }
    buf[n] = 0;
    return buf[0 .. n + 1];
}

fn applyClipboard(cmd: []const u8) bool {
    if (cmd.len < 5) return false;
    var i: usize = 0;
    var command: u32 = 0;
    var digits: usize = 0;
    while (i < cmd.len and cmd[i] >= '0' and cmd[i] <= '9') : (i += 1) {
        command = command * 10 + (cmd[i] - '0');
        digits += 1;
    }
    if (digits == 0 or i >= cmd.len or cmd[i] != ';' or command != 52) return false;
    i += 1;
    while (i < cmd.len and cmd[i] != ';') : (i += 1) {}
    if (i >= cmd.len) return false;
    i += 1;
    const payload = cmd[i..];

    if (payload.len == 1 and payload[0] == '?') return false;
    if (payload.len == 0) {
        rl.SetClipboardText("");
        return true;
    }

    const decoded = base64Decode(payload) catch return false;
    defer std.heap.c_allocator.free(decoded);
    rl.SetClipboardText(@as([*c]const u8, @ptrCast(decoded.ptr)));
    return true;
}

fn processOscByte(s: *State, b: u8) void {
    if ((!s.osc_cmd_decided or s.osc_collect) and !appendByte(s, b)) {
        s.osc_cmd_decided = true;
        s.osc_collect = false;
        s.osc_len = 0;
    }
    if (s.osc_cmd_decided) return;
    if (b >= '0' and b <= '9') {
        s.osc_cmd = s.osc_cmd * 10 + (b - '0');
        s.osc_cmd_digits += 1;
        return;
    }
    if (b == ';') {
        s.osc_cmd_decided = true;
        s.osc_collect = s.osc_cmd_digits > 0 and s.osc_cmd == 52;
        if (!s.osc_collect) s.osc_len = 0;
        return;
    }
    s.osc_cmd_decided = true;
    s.osc_collect = false;
    s.osc_len = 0;
}

fn finishOsc(s: *State) void {
    if (s.osc_collect and s.osc_len > 0) {
        const ptr = s.osc_buf orelse return;
        _ = applyClipboard(ptr[0..s.osc_len]);
    }
    s.mode = 0;
    resetCommand(s);
}

export fn osc52_clipboard_scan(s: *State, data: [*c]const u8, len: usize) void {
    if (len == 0) return;
    for (data[0..len]) |b| {
        switch (s.mode) {
            0 => {
                if (b == 0x1B) s.mode = 1;
            },
            1 => {
                if (b == ']') {
                    s.mode = 2;
                    resetCommand(s);
                } else if (b == 0x1B) {
                    s.mode = 1;
                } else {
                    s.mode = 0;
                }
            },
            2 => {
                if (s.osc_esc_pending) {
                    s.osc_esc_pending = false;
                    if (b == '\\') {
                        finishOsc(s);
                    } else {
                        processOscByte(s, 0x1B);
                        processOscByte(s, b);
                    }
                } else if (b == 0x07) {
                    finishOsc(s);
                } else if (b == 0x1B) {
                    s.osc_esc_pending = true;
                } else {
                    processOscByte(s, b);
                }
            },
            else => s.mode = 0,
        }
    }
}
