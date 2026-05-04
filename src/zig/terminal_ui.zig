const std = @import("std");
const builtin = @import("builtin");

const c = @cImport({
    @cInclude("terminal_ui.h");
    @cInclude("raylib.h");
    @cInclude("ghostty/vt.h");
    @cInclude("ghostty/vt/render.h");
    @cInclude("config_font.h");
    @cInclude("stdio.h");
    @cInclude("string.h");
    @cInclude("stdlib.h");
    @cInclude("spawn.h");
    @cInclude("ctype.h");
});

const ByteBuffer = struct {
    ptr: [*c]u8,
    len: usize,
    cap: usize,

    fn init(capacity: usize) ?ByteBuffer {
        const mem = c.malloc(capacity) orelse return null;
        const ptr: [*c]u8 = @ptrCast(mem);
        ptr[0] = 0;
        return .{ .ptr = ptr, .len = 0, .cap = capacity };
    }

    fn deinit(self: *ByteBuffer) void {
        c.free(self.ptr);
        self.ptr = null;
        self.len = 0;
        self.cap = 0;
    }

    fn append(self: *ByteBuffer, src: [*c]const u8, src_len: usize) bool {
        return appendBytes(&self.ptr, &self.len, &self.cap, src, src_len);
    }

    fn appendCodepoint(self: *ByteBuffer, cp: u32) bool {
        return appendUtf8Codepoint(&self.ptr, &self.len, &self.cap, cp);
    }

    fn trimRightSpaces(self: *ByteBuffer, start: usize) void {
        while (self.len > start and self.ptr[self.len - 1] == ' ') {
            self.len -= 1;
            self.ptr[self.len] = 0;
        }
    }
};

// ============================================================================
// Helper functions
// ============================================================================

fn viewportRowIsSoftWrapped(terminal: c.GhosttyTerminal, row: u16) bool {
    var point = std.mem.zeroes(c.GhosttyPoint);
    point.tag = c.GHOSTTY_POINT_TAG_VIEWPORT;
    point.value.coordinate.x = 0;
    point.value.coordinate.y = row;
    var ref: c.GhosttyGridRef = undefined;
    @memset(@as([*]u8, @ptrCast(&ref))[0..@sizeOf(c.GhosttyGridRef)], 0);
    ref.size = @sizeOf(c.GhosttyGridRef);
    if (c.ghostty_terminal_grid_ref(terminal, point, &ref) != c.GHOSTTY_SUCCESS) return false;
    var r: c.GhosttyRow = 0;
    if (c.ghostty_grid_ref_row(&ref, &r) != c.GHOSTTY_SUCCESS) return false;
    var wrap: bool = false;
    if (c.ghostty_row_get(r, c.GHOSTTY_ROW_DATA_WRAP, &wrap) != c.GHOSTTY_SUCCESS) return false;
    return wrap;
}

fn appendBytes(buf: *[*c]u8, len: *usize, cap: *usize, src: [*c]const u8, src_len: usize) bool {
    if (src_len == 0) return true;
    if (len.* + src_len + 1 > cap.*) {
        var new_cap = cap.*;
        while (len.* + src_len + 1 > new_cap) new_cap = if (new_cap < 1024) new_cap * 2 else new_cap + 1024;
        const new_buf = c.realloc(@as(?*anyopaque, @ptrCast(buf.*)), new_cap);
        if (new_buf == null) return false;
        buf.* = @as([*c]u8, @ptrCast(new_buf));
        cap.* = new_cap;
    }
    @memcpy(@as([*]u8, @ptrCast(buf.*))[len.* .. len.* + src_len], src[0..src_len]);
    len.* += src_len;
    buf.*[len.*] = 0;
    return true;
}

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

fn appendUtf8Codepoint(buf: *[*c]u8, len: *usize, cap: *usize, cp: u32) bool {
    var u8_buf: [4]u8 = undefined;
    // Pass pointer to first element of u8_buf
    const n = utf8Encode(cp, @as([*c]u8, @ptrCast(&u8_buf[0])));
    return appendBytes(buf, len, cap, @as([*c]const u8, @ptrCast(&u8_buf[0])), @as(usize, @intCast(n)));
}

fn appendCellTextToBuffer(terminal: c.GhosttyTerminal, x: u16, y: u16, out: *[*c]u8, out_len: *usize, out_cap: *usize) bool {
    var point = std.mem.zeroes(c.GhosttyPoint);
    point.tag = c.GHOSTTY_POINT_TAG_VIEWPORT;
    point.value.coordinate.x = x;
    point.value.coordinate.y = y;
    var ref: c.GhosttyGridRef = undefined;
    @memset(@as([*]u8, @ptrCast(&ref))[0..@sizeOf(c.GhosttyGridRef)], 0);
    ref.size = @sizeOf(c.GhosttyGridRef);
    if (c.ghostty_terminal_grid_ref(terminal, point, &ref) != c.GHOSTTY_SUCCESS) return false;
    var cell: c.GhosttyCell = 0;
    if (c.ghostty_grid_ref_cell(&ref, &cell) != c.GHOSTTY_SUCCESS) return false;
    var wide: c.GhosttyCellWide = c.GHOSTTY_CELL_WIDE_NARROW;
    if (c.ghostty_cell_get(cell, c.GHOSTTY_CELL_DATA_WIDE, &wide) == c.GHOSTTY_SUCCESS and
        (wide == c.GHOSTTY_CELL_WIDE_SPACER_TAIL or wide == c.GHOSTTY_CELL_WIDE_SPACER_HEAD)) return false;
    var cps_inline: [16]u32 = undefined;
    var cp_len: usize = 0;
    const gr = c.ghostty_grid_ref_graphemes(&ref, &cps_inline, cps_inline.len, &cp_len);
    if (gr == c.GHOSTTY_OUT_OF_SPACE and cp_len > 0) {
        const dyn = c.malloc(cp_len * @sizeOf(u32));
        if (dyn == null) return false;
        const gr2 = c.ghostty_grid_ref_graphemes(&ref, @as([*c]u32, @ptrCast(@alignCast(dyn))), cp_len, &cp_len);
        if (gr2 == c.GHOSTTY_SUCCESS) {
            const slices = @as([*]u32, @ptrCast(@alignCast(dyn)))[0..cp_len];
            for (slices) |cp2| {
                if (!appendUtf8Codepoint(out, out_len, out_cap, cp2)) {
                    c.free(dyn);
                    return false;
                }
            }
            c.free(dyn);
            return true;
        }
        c.free(dyn);
        return false;
    }
    if (gr == c.GHOSTTY_SUCCESS and cp_len > 0) {
        for (cps_inline[0..cp_len]) |cp2| {
            if (!appendUtf8Codepoint(out, out_len, out_cap, cp2)) return false;
        }
        return true;
    }
    return false;
}

fn collectRowText(terminal: c.GhosttyTerminal, term_cols: u16, y: u16, out: *[*c]u8, out_len: *usize, out_cap: *usize) bool {
    var x: u16 = 0;
    while (x < term_cols) : (x += 1) {
        if (!appendCellTextToBuffer(terminal, x, y, out, out_len, out_cap)) {
            if (!appendBytes(out, out_len, out_cap, " ", 1)) return false;
        }
    }
    while (out_len.* > 0 and out.*[out_len.* - 1] == ' ') {
        out_len.* -= 1;
        out.*[out_len.*] = 0;
    }
    return true;
}

fn scanFirstUrlInText(text: [*c]const u8, url_out: [*c]u8, url_out_sz: usize) bool {
    const scheme = c.strstr(text, "https://");
    const scheme_http = c.strstr(text, "http://");
    const actual_scheme = if (scheme == null or (scheme_http != null and scheme_http < scheme)) scheme_http else scheme;
    if (actual_scheme == null) return false;
    var end = actual_scheme;
    while (end.* != 0 and c.isspace(@as(c_int, end.*)) == 0) end += 1;
    while (@intFromPtr(end) > @intFromPtr(actual_scheme)) {
        const prev = end - 1;
        const ch = @as(u8, prev.*);
        if (c.strchr(")]}'\".,;:!?", @as(c_int, ch)) == null) break;
        end -= 1;
    }
    if (end <= actual_scheme) return false;
    const len = @min(@as(usize, @intFromPtr(end) - @intFromPtr(actual_scheme)), url_out_sz - 1);
    @memcpy(url_out[0..len], actual_scheme[0..len]);
    url_out[len] = 0;
    return len > 0;
}

fn ghosttyModsFromRaylib() c.GhosttyMods {
    var mods: c.GhosttyMods = 0;
    if (c.IsKeyDown(c.KEY_LEFT_SHIFT) or c.IsKeyDown(c.KEY_RIGHT_SHIFT)) mods |= c.GHOSTTY_MODS_SHIFT;
    if (c.IsKeyDown(c.KEY_LEFT_CONTROL) or c.IsKeyDown(c.KEY_RIGHT_CONTROL)) mods |= c.GHOSTTY_MODS_CTRL;
    if (c.IsKeyDown(c.KEY_LEFT_ALT) or c.IsKeyDown(c.KEY_RIGHT_ALT)) mods |= c.GHOSTTY_MODS_ALT;
    if (c.IsKeyDown(c.KEY_LEFT_SUPER) or c.IsKeyDown(c.KEY_RIGHT_SUPER)) mods |= c.GHOSTTY_MODS_SUPER;
    return mods;
}

fn raylibKeyUnshiftedCodepoint(rl_key: c_int) u32 {
    if (rl_key >= c.KEY_A and rl_key <= c.KEY_Z) return @as(u32, @intCast('a' + (rl_key - c.KEY_A)));
    if (rl_key >= c.KEY_ZERO and rl_key <= c.KEY_NINE) return @as(u32, @intCast('0' + (rl_key - c.KEY_ZERO)));
    return switch (rl_key) {
        c.KEY_SPACE => ' ',
        c.KEY_MINUS => '-',
        c.KEY_EQUAL => '=',
        c.KEY_LEFT_BRACKET => '[',
        c.KEY_RIGHT_BRACKET => ']',
        c.KEY_BACKSLASH => '\\',
        c.KEY_SEMICOLON => ';',
        c.KEY_APOSTROPHE => '\'',
        c.KEY_COMMA => ',',
        c.KEY_PERIOD => '.',
        c.KEY_SLASH => '/',
        c.KEY_GRAVE => '`',
        else => 0,
    };
}

fn raylibMouseToGhostty(rl_button: c_int) c.GhosttyMouseButton {
    return switch (rl_button) {
        c.MOUSE_BUTTON_LEFT => c.GHOSTTY_MOUSE_BUTTON_LEFT,
        c.MOUSE_BUTTON_RIGHT => c.GHOSTTY_MOUSE_BUTTON_RIGHT,
        c.MOUSE_BUTTON_MIDDLE => c.GHOSTTY_MOUSE_BUTTON_MIDDLE,
        c.MOUSE_BUTTON_SIDE => c.GHOSTTY_MOUSE_BUTTON_FOUR,
        c.MOUSE_BUTTON_EXTRA => c.GHOSTTY_MOUSE_BUTTON_FIVE,
        c.MOUSE_BUTTON_FORWARD => c.GHOSTTY_MOUSE_BUTTON_SIX,
        c.MOUSE_BUTTON_BACK => c.GHOSTTY_MOUSE_BUTTON_SEVEN,
        else => c.GHOSTTY_MOUSE_BUTTON_UNKNOWN,
    };
}

fn mouseDebugEnabled() bool {
    const cached = struct {
        var value: i32 = -1;
    };
    if (cached.value < 0) {
        const v = c.getenv("GHOSTLING_DEBUG_MOUSE");
        cached.value = if (v != null and v[0] != 0 and c.strcmp(v, "0") != 0) 1 else 0;
        if (cached.value == 1) {
            _ = c.fprintf(c.stderr(), "ghostling: mouse debug logging enabled (GHOSTLING_DEBUG_MOUSE)\n");
        }
    }
    return cached.value == 1;
}

fn mouseDebugLogEncode(event: c.GhosttyMouseEvent, res: c.GhosttyResult, buf: [*c]const u8, written: usize) void {
    if (!mouseDebugEnabled()) return;

    const action = c.ghostty_mouse_event_get_action(event);
    var button: c.GhosttyMouseButton = c.GHOSTTY_MOUSE_BUTTON_UNKNOWN;
    const has_button = c.ghostty_mouse_event_get_button(event, &button);
    const mods = c.ghostty_mouse_event_get_mods(event);
    const pos = c.ghostty_mouse_event_get_position(event);

    _ = c.fprintf(c.stderr(), "ghostling: mouse encode action=%d button=%s%d mods=0x%X pos=(%.1f,%.1f) res=%d written=%zu seq=\"", @as(c_int, @intCast(action)), if (has_button) @as([*c]const u8, @ptrCast("")) else @as([*c]const u8, @ptrCast("none/")), @as(c_int, @intCast(button)), @as(c_uint, @intCast(mods)), pos.x, pos.y, @as(c_int, @intCast(res)), written);

    var i: usize = 0;
    while (i < written) : (i += 1) {
        const ch = buf[i];
        if (ch == '\\') {
            _ = c.fputs("\\\\", c.stderr());
        } else if (ch == 0x1B) {
            _ = c.fputs("\\x1b", c.stderr());
        } else if (ch >= 0x20 and ch <= 0x7E) {
            _ = c.fputc(@as(c_int, @intCast(ch)), c.stderr());
        } else {
            _ = c.fprintf(c.stderr(), "\\x%02X", @as(c_uint, @intCast(ch)));
        }
    }

    _ = c.fputs("\"\n", c.stderr());
}

fn mouseEncodeAndWrite(pty_fd: c.PtyHandle, encoder: c.GhosttyMouseEncoder, event: c.GhosttyMouseEvent) bool {
    var buf: [128]u8 = undefined;
    var written: usize = 0;
    const buf_ptr: [*c]u8 = @ptrCast(&buf[0]);
    const res = c.ghostty_mouse_encoder_encode(encoder, event, buf_ptr, buf.len, &written);

    mouseDebugLogEncode(event, res, @ptrCast(buf_ptr), written);

    if (res == c.GHOSTTY_SUCCESS and written > 0) {
        c.pty_write(pty_fd, @ptrCast(buf_ptr), written);
        return true;
    }
    return false;
}

fn selectionContainsCell(selection_active: bool, sel_x0: u16, sel_y0: u16, sel_x1: u16, sel_y1: u16, x: u16, y: u16) bool {
    if (!selection_active) return false;

    var sx0 = sel_x0;
    var sy0 = sel_y0;
    var sx1 = sel_x1;
    var sy1 = sel_y1;

    if (sy0 > sy1 or (sy0 == sy1 and sx0 > sx1)) {
        const tx = sx0;
        const ty = sy0;
        sx0 = sx1;
        sy0 = sy1;
        sx1 = tx;
        sy1 = ty;
    }

    if (y < sy0 or y > sy1) return false;
    if (y == sy0 and x < sx0) return false;
    if (y == sy1 and x > sx1) return false;
    return true;
}

// ============================================================================
// Exported functions
// ============================================================================

export fn cell_has_hyperlink(terminal: c.GhosttyTerminal, x: u16, y: u16) bool {
    var point = std.mem.zeroes(c.GhosttyPoint);
    point.tag = c.GHOSTTY_POINT_TAG_VIEWPORT;
    point.value.coordinate.x = x;
    point.value.coordinate.y = y;
    var ref: c.GhosttyGridRef = undefined;
    @memset(@as([*]u8, @ptrCast(&ref))[0..@sizeOf(c.GhosttyGridRef)], 0);
    ref.size = @sizeOf(c.GhosttyGridRef);
    if (c.ghostty_terminal_grid_ref(terminal, point, &ref) != c.GHOSTTY_SUCCESS) return false;
    var cell: c.GhosttyCell = 0;
    if (c.ghostty_grid_ref_cell(&ref, &cell) != c.GHOSTTY_SUCCESS) return false;
    var has_hl: bool = false;
    return c.ghostty_cell_get(cell, c.GHOSTTY_CELL_DATA_HAS_HYPERLINK, &has_hl) == c.GHOSTTY_SUCCESS and has_hl;
}

export fn open_url_at_cell(terminal: c.GhosttyTerminal, term_cols: u16, term_rows: u16, x: u16, y: u16) bool {
    if (!cell_has_hyperlink(terminal, x, y)) return false;
    var cap = @as(usize, @intCast(term_cols)) * 8 + 64;
    if (cap < 256) cap = 256;
    const row_text = c.malloc(cap);
    if (row_text == null) return false;
    var row_len: usize = 0;
    @memset(@as([*]u8, @ptrCast(row_text))[0..cap], 0);
    var buf: [*c]u8 = @as([*c]u8, @ptrCast(row_text));
    if (!collectRowText(terminal, term_cols, y, &buf, &row_len, &cap)) {
        c.free(row_text);
        return false;
    }
    if (y + 1 < term_rows and viewportRowIsSoftWrapped(terminal, y)) {
        if (!appendBytes(&buf, &row_len, &cap, " ", 1) or !collectRowText(terminal, term_cols, @as(u16, @intCast(y + 1)), &buf, &row_len, &cap)) {
            c.free(row_text);
            return false;
        }
    }
    var url: [2048]u8 = undefined;
    const ok = scanFirstUrlInText(buf, @ptrCast(&url[0]), url.len);
    c.free(row_text);
    if (!ok) return false;
    if (builtin.os.tag == .macos) {
        var pid: c.pid_t = 0;
        const open_str: [*:0]const u8 = "open";
        var argv: [3][*c]const u8 = .{ open_str, @as([*:0]const u8, @ptrCast(&url[0])), null };
        const envp: [*c]const [*c]const u8 = @extern([*c]const [*c]const u8, .{ .name = "environ" });
        return c.posix_spawnp(&pid, "open", null, null, @ptrCast(&argv), @ptrCast(envp)) == 0;
    }
    if (builtin.os.tag == .linux) {
        var pid: c.pid_t = 0;
        const xdg_str: [*:0]const u8 = "xdg-open";
        var argv: [3][*c]const u8 = .{ xdg_str, @as([*:0]const u8, @ptrCast(&url[0])), null };
        const envp: [*c]const [*c]const u8 = @extern([*c]const [*c]const u8, .{ .name = "environ" });
        return c.posix_spawnp(&pid, "xdg-open", null, null, @ptrCast(&argv), @ptrCast(envp)) == 0;
    }
    return false;
}

export fn handle_mouse(pty_fd: c.PtyHandle, encoder: c.GhosttyMouseEncoder, event: c.GhosttyMouseEvent, terminal: c.GhosttyTerminal, cell_width: c_int, cell_height: c_int, pad_left: c_int, pad_top: c_int, pad_right: c_int, pad_bottom: c_int, screen_width: c_int, screen_height: c_int) bool {
    var had_event = false;
    c.ghostty_mouse_encoder_setopt_from_terminal(encoder, terminal);

    const scr_w = if (screen_width > 0) screen_width else c.GetScreenWidth();
    const scr_h = if (screen_height > 0) screen_height else c.GetScreenHeight();
    const enc_size = c.GhosttyMouseEncoderSize{
        .size = @sizeOf(c.GhosttyMouseEncoderSize),
        .screen_width = @as(u32, @intCast(scr_w)),
        .screen_height = @as(u32, @intCast(scr_h)),
        .cell_width = @as(u32, @intCast(cell_width)),
        .cell_height = @as(u32, @intCast(cell_height)),
        .padding_top = @as(u32, @intCast(pad_top)),
        .padding_bottom = @as(u32, @intCast(pad_bottom)),
        .padding_left = @as(u32, @intCast(pad_left)),
        .padding_right = @as(u32, @intCast(pad_right)),
    };
    _ = c.ghostty_mouse_encoder_setopt(encoder, c.GHOSTTY_MOUSE_ENCODER_OPT_SIZE, &enc_size);

    const any_pressed = c.IsMouseButtonDown(c.MOUSE_BUTTON_LEFT) or
        c.IsMouseButtonDown(c.MOUSE_BUTTON_RIGHT) or
        c.IsMouseButtonDown(c.MOUSE_BUTTON_MIDDLE);
    _ = c.ghostty_mouse_encoder_setopt(encoder, c.GHOSTTY_MOUSE_ENCODER_OPT_ANY_BUTTON_PRESSED, &any_pressed);

    var track_cell = true;
    _ = c.ghostty_mouse_encoder_setopt(encoder, c.GHOSTTY_MOUSE_ENCODER_OPT_TRACK_LAST_CELL, &track_cell);

    const mods = ghosttyModsFromRaylib();
    const pos = c.GetMousePosition();
    _ = c.ghostty_mouse_event_set_mods(event, mods);
    _ = c.ghostty_mouse_event_set_position(event, .{ .x = pos.x, .y = pos.y });

    const WheelAccum = struct {
        var accum: f32 = 0.0;
        var backlog: i32 = 0;
    };
    WheelAccum.accum += c.GetMouseWheelMove();

    var wheel_steps: i32 = 0;
    while (WheelAccum.accum >= 1.0) {
        wheel_steps += 1;
        WheelAccum.accum -= 1.0;
    }
    while (WheelAccum.accum <= -1.0) {
        wheel_steps -= 1;
        WheelAccum.accum += 1.0;
    }

    if (wheel_steps != 0) {
        WheelAccum.backlog += wheel_steps;
        if (WheelAccum.backlog > 64)
            WheelAccum.backlog = 64
        else if (WheelAccum.backlog < -64)
            WheelAccum.backlog = -64;
    }

    const buttons = [_]c_int{ c.MOUSE_BUTTON_LEFT, c.MOUSE_BUTTON_RIGHT, c.MOUSE_BUTTON_MIDDLE };
    for (buttons) |rl_btn| {
        const gbtn = raylibMouseToGhostty(rl_btn);
        if (gbtn == c.GHOSTTY_MOUSE_BUTTON_UNKNOWN) continue;

        if (c.IsMouseButtonPressed(rl_btn)) {
            _ = c.ghostty_mouse_event_set_action(event, c.GHOSTTY_MOUSE_ACTION_PRESS);
            _ = c.ghostty_mouse_event_set_button(event, gbtn);
            if (mouseEncodeAndWrite(pty_fd, encoder, event)) had_event = true;
        } else if (c.IsMouseButtonReleased(rl_btn)) {
            _ = c.ghostty_mouse_event_set_action(event, c.GHOSTTY_MOUSE_ACTION_RELEASE);
            _ = c.ghostty_mouse_event_set_button(event, gbtn);
            if (mouseEncodeAndWrite(pty_fd, encoder, event)) had_event = true;
        }
    }

    const delta = c.GetMouseDelta();
    if (WheelAccum.backlog == 0 and (delta.x != 0.0 or delta.y != 0.0)) {
        _ = c.ghostty_mouse_event_set_action(event, c.GHOSTTY_MOUSE_ACTION_MOTION);
        if (c.IsMouseButtonDown(c.MOUSE_BUTTON_LEFT))
            _ = c.ghostty_mouse_event_set_button(event, c.GHOSTTY_MOUSE_BUTTON_LEFT)
        else if (c.IsMouseButtonDown(c.MOUSE_BUTTON_RIGHT))
            _ = c.ghostty_mouse_event_set_button(event, c.GHOSTTY_MOUSE_BUTTON_RIGHT)
        else if (c.IsMouseButtonDown(c.MOUSE_BUTTON_MIDDLE))
            _ = c.ghostty_mouse_event_set_button(event, c.GHOSTTY_MOUSE_BUTTON_MIDDLE)
        else
            c.ghostty_mouse_event_clear_button(event);
        if (mouseEncodeAndWrite(pty_fd, encoder, event)) had_event = true;
    }

    if (WheelAccum.backlog != 0) {
        var mouse_tracking = false;
        _ = c.ghostty_terminal_get(terminal, c.GHOSTTY_TERMINAL_DATA_MOUSE_TRACKING, &mouse_tracking);

        var dispatch_steps = WheelAccum.backlog;
        if (mouse_tracking)
            dispatch_steps = if (WheelAccum.backlog > 0) 1 else -1;

        if (mouseDebugEnabled())
            _ = c.fprintf(c.stderr(), "ghostling: mouse wheel backlog=%d dispatch=%d tracking=%d\n", WheelAccum.backlog, dispatch_steps, if (mouse_tracking) @as(c_int, 1) else @as(c_int, 0));

        const scroll_btn = if (dispatch_steps > 0) c.GHOSTTY_MOUSE_BUTTON_FOUR else c.GHOSTTY_MOUSE_BUTTON_FIVE;
        const repeats = if (dispatch_steps > 0) dispatch_steps else -dispatch_steps;
        var sent_to_pty = false;
        _ = c.ghostty_mouse_event_set_button(event, @intCast(scroll_btn));
        _ = c.ghostty_mouse_event_set_action(event, c.GHOSTTY_MOUSE_ACTION_PRESS);
        var i: i32 = 0;
        while (i < repeats) : (i += 1) {
            if (mouseEncodeAndWrite(pty_fd, encoder, event)) sent_to_pty = true;
        }

        if (!sent_to_pty and !mouse_tracking) {
            const scroll_delta = -3 * dispatch_steps;
            const sv = c.GhosttyTerminalScrollViewport{
                .tag = c.GHOSTTY_SCROLL_VIEWPORT_DELTA,
                .value = .{ .delta = scroll_delta },
            };
            _ = c.ghostty_terminal_scroll_viewport(terminal, sv);
        }

        WheelAccum.backlog -= dispatch_steps;
        had_event = true;
    }

    return had_event;
}

export fn handle_input(pty_fd: c.PtyHandle, encoder: c.GhosttyKeyEncoder, event: c.GhosttyKeyEvent, terminal: c.GhosttyTerminal) bool {
    var had_event = false;
    c.ghostty_key_encoder_setopt_from_terminal(encoder, terminal);

    var char_utf8: [64]u8 = undefined;
    var char_utf8_len: usize = 0;
    while (true) {
        const ch = c.GetCharPressed();
        if (ch == 0) break;
        var u8_buf: [4]u8 = undefined;
        const n = utf8Encode(@as(u32, @intCast(ch)), @as([*c]u8, @ptrCast(&u8_buf[0])));
        if (char_utf8_len + @as(usize, @intCast(n)) < char_utf8.len) {
            @memcpy(char_utf8[char_utf8_len .. char_utf8_len + @as(usize, @intCast(n))], u8_buf[0..@as(usize, @intCast(n))]);
            char_utf8_len += @as(usize, @intCast(n));
            had_event = true;
        }
    }

    const special_keys = [_]c_int{
        c.KEY_SPACE,     c.KEY_ENTER,        c.KEY_TAB,           c.KEY_BACKSPACE,
        c.KEY_DELETE,    c.KEY_ESCAPE,       c.KEY_UP,            c.KEY_DOWN,
        c.KEY_LEFT,      c.KEY_RIGHT,        c.KEY_HOME,          c.KEY_END,
        c.KEY_PAGE_UP,   c.KEY_PAGE_DOWN,    c.KEY_INSERT,        c.KEY_MINUS,
        c.KEY_EQUAL,     c.KEY_LEFT_BRACKET, c.KEY_RIGHT_BRACKET, c.KEY_BACKSLASH,
        c.KEY_SEMICOLON, c.KEY_APOSTROPHE,   c.KEY_COMMA,         c.KEY_PERIOD,
        c.KEY_SLASH,     c.KEY_GRAVE,        c.KEY_F1,            c.KEY_F2,
        c.KEY_F3,        c.KEY_F4,           c.KEY_F5,            c.KEY_F6,
        c.KEY_F7,        c.KEY_F8,           c.KEY_F9,            c.KEY_F10,
        c.KEY_F11,       c.KEY_F12,
    };

    var keys_to_check: [26 + 10 + special_keys.len]c_int = undefined;
    var num_keys: usize = 0;
    var k: c_int = c.KEY_A;
    while (k <= c.KEY_Z) : (k += 1) {
        keys_to_check[num_keys] = k;
        num_keys += 1;
    }
    k = c.KEY_ZERO;
    while (k <= c.KEY_NINE) : (k += 1) {
        keys_to_check[num_keys] = k;
        num_keys += 1;
    }
    for (special_keys) |sk| {
        keys_to_check[num_keys] = sk;
        num_keys += 1;
    }

    const mods = ghosttyModsFromRaylib();

    for (keys_to_check[0..num_keys]) |rl_key| {
        const pressed = c.IsKeyPressed(rl_key);
        const repeated = c.IsKeyPressedRepeat(rl_key);
        const released = c.IsKeyReleased(rl_key);
        if (!pressed and !repeated and !released) continue;
        had_event = true;

        const gkey = raylibKeyToGhostty(rl_key);
        if (gkey == c.GHOSTTY_KEY_UNIDENTIFIED) continue;

        const action: c_uint = if (released) c.GHOSTTY_KEY_ACTION_RELEASE else if (pressed) c.GHOSTTY_KEY_ACTION_PRESS else c.GHOSTTY_KEY_ACTION_REPEAT;

        _ = c.ghostty_key_event_set_key(event, gkey);
        _ = c.ghostty_key_event_set_action(event, @intCast(action));
        _ = c.ghostty_key_event_set_mods(event, mods);

        const ucp = raylibKeyUnshiftedCodepoint(rl_key);
        _ = c.ghostty_key_event_set_unshifted_codepoint(event, ucp);

        var consumed: c.GhosttyMods = 0;
        if (ucp != 0 and (mods & c.GHOSTTY_MODS_SHIFT) != 0)
            consumed |= c.GHOSTTY_MODS_SHIFT;
        _ = c.ghostty_key_event_set_consumed_mods(event, consumed);

        if (char_utf8_len > 0 and !released) {
            _ = c.ghostty_key_event_set_utf8(event, @as([*c]const u8, @ptrCast(&char_utf8)), char_utf8_len);
            char_utf8_len = 0;
        } else {
            _ = c.ghostty_key_event_set_utf8(event, null, 0);
        }

        var buf: [128]u8 = undefined;
        var written: usize = 0;
        const buf_ptr: [*c]u8 = @ptrCast(&buf[0]);
        const res = c.ghostty_key_encoder_encode(encoder, event, buf_ptr, buf.len, &written);
        if (res == c.GHOSTTY_SUCCESS and written > 0) {
            c.pty_write(pty_fd, @ptrCast(buf_ptr), written);
            char_utf8_len = 0;
        }
    }

    if (char_utf8_len > 0)
        c.pty_write(pty_fd, @ptrCast(&char_utf8[0]), char_utf8_len);

    return had_event;
}

fn raylibKeyToGhostty(rl_key: c_int) c.GhosttyKey {
    if (rl_key >= c.KEY_A and rl_key <= c.KEY_Z) return @as(c.GhosttyKey, @intCast(@as(c_uint, @intCast(c.GHOSTTY_KEY_A)) + @as(c_uint, @intCast(rl_key - c.KEY_A))));
    if (rl_key >= c.KEY_ZERO and rl_key <= c.KEY_NINE) return @as(c.GhosttyKey, @intCast(@as(c_uint, @intCast(c.GHOSTTY_KEY_DIGIT_0)) + @as(c_uint, @intCast(rl_key - c.KEY_ZERO))));
    if (rl_key >= c.KEY_F1 and rl_key <= c.KEY_F12) return @as(c.GhosttyKey, @intCast(@as(c_uint, @intCast(c.GHOSTTY_KEY_F1)) + @as(c_uint, @intCast(rl_key - c.KEY_F1))));
    return switch (rl_key) {
        c.KEY_SPACE => c.GHOSTTY_KEY_SPACE,
        c.KEY_ENTER => c.GHOSTTY_KEY_ENTER,
        c.KEY_TAB => c.GHOSTTY_KEY_TAB,
        c.KEY_BACKSPACE => c.GHOSTTY_KEY_BACKSPACE,
        c.KEY_DELETE => c.GHOSTTY_KEY_DELETE,
        c.KEY_ESCAPE => c.GHOSTTY_KEY_ESCAPE,
        c.KEY_UP => c.GHOSTTY_KEY_ARROW_UP,
        c.KEY_DOWN => c.GHOSTTY_KEY_ARROW_DOWN,
        c.KEY_LEFT => c.GHOSTTY_KEY_ARROW_LEFT,
        c.KEY_RIGHT => c.GHOSTTY_KEY_ARROW_RIGHT,
        c.KEY_HOME => c.GHOSTTY_KEY_HOME,
        c.KEY_END => c.GHOSTTY_KEY_END,
        c.KEY_PAGE_UP => c.GHOSTTY_KEY_PAGE_UP,
        c.KEY_PAGE_DOWN => c.GHOSTTY_KEY_PAGE_DOWN,
        c.KEY_INSERT => c.GHOSTTY_KEY_INSERT,
        c.KEY_MINUS => c.GHOSTTY_KEY_MINUS,
        c.KEY_EQUAL => c.GHOSTTY_KEY_EQUAL,
        c.KEY_LEFT_BRACKET => c.GHOSTTY_KEY_BRACKET_LEFT,
        c.KEY_RIGHT_BRACKET => c.GHOSTTY_KEY_BRACKET_RIGHT,
        c.KEY_BACKSLASH => c.GHOSTTY_KEY_BACKSLASH,
        c.KEY_SEMICOLON => c.GHOSTTY_KEY_SEMICOLON,
        c.KEY_APOSTROPHE => c.GHOSTTY_KEY_QUOTE,
        c.KEY_COMMA => c.GHOSTTY_KEY_COMMA,
        c.KEY_PERIOD => c.GHOSTTY_KEY_PERIOD,
        c.KEY_SLASH => c.GHOSTTY_KEY_SLASH,
        c.KEY_GRAVE => c.GHOSTTY_KEY_BACKQUOTE,
        else => c.GHOSTTY_KEY_UNIDENTIFIED,
    };
}

export fn handle_scrollbar(terminal: c.GhosttyTerminal, render_state: c.GhosttyRenderState, dragging: *bool, grid_origin_x: c_int, grid_origin_y: c_int, term_rows: u16, cell_height: c_int, pad_right: c_int) bool {
    const mpos = c.GetMousePosition();
    if (mpos.x < @as(f32, @floatFromInt(grid_origin_x))) {
        if (c.IsMouseButtonReleased(c.MOUSE_BUTTON_LEFT))
            dragging.* = false;
        return false;
    }

    var scrollbar = std.mem.zeroes(c.GhosttyTerminalScrollbar);
    if (c.ghostty_terminal_get(terminal, c.GHOSTTY_TERMINAL_DATA_SCROLLBAR, &scrollbar) != c.GHOSTTY_SUCCESS)
        return false;

    if (scrollbar.total <= scrollbar.len) {
        dragging.* = false;
        return false;
    }

    const scr_w = c.GetScreenWidth();
    var track_h = @as(c_int, @intCast(term_rows)) * cell_height;
    if (track_h < 1) track_h = 1;

    const bar_width: c_int = 6;
    const bar_margin: c_int = 2;
    const bar_left = scr_w - pad_right - bar_width - bar_margin;
    const hit_left = bar_left - 8;
    if (c.IsMouseButtonPressed(c.MOUSE_BUTTON_LEFT) and mpos.x >= @as(f32, @floatFromInt(hit_left)) and
        mpos.x <= @as(f32, @floatFromInt(scr_w)) and mpos.y >= @as(f32, @floatFromInt(grid_origin_y)) and
        mpos.y <= @as(f32, @floatFromInt(grid_origin_y + track_h)))
    {
        dragging.* = true;
    }

    if (dragging.* and c.IsMouseButtonDown(c.MOUSE_BUTTON_LEFT)) {
        const scrollable = @as(f64, @floatFromInt(scrollbar.total - scrollbar.len));
        var frac = (@as(f64, @floatCast(mpos.y)) - @as(f64, @floatFromInt(grid_origin_y))) / @as(f64, @floatFromInt(track_h));
        if (frac < 0.0) frac = 0.0;
        if (frac > 1.0) frac = 1.0;
        const target = @as(i64, @intFromFloat(frac * scrollable));

        const delta = @as(isize, @intCast(target - @as(i64, @intCast(scrollbar.offset))));
        if (delta != 0) {
            const sv = c.GhosttyTerminalScrollViewport{
                .tag = c.GHOSTTY_SCROLL_VIEWPORT_DELTA,
                .value = .{ .delta = @as(i32, @intCast(delta)) },
            };
            _ = c.ghostty_terminal_scroll_viewport(terminal, sv);
            _ = c.ghostty_render_state_update(render_state, terminal);
        }
    }

    if (c.IsMouseButtonReleased(c.MOUSE_BUTTON_LEFT))
        dragging.* = false;

    return dragging.*;
}

export fn copy_viewport_selection_to_clipboard(terminal: c.GhosttyTerminal, term_cols: u16, term_rows: u16, sel_x0: u16, sel_y0: u16, sel_x1: u16, sel_y1: u16) bool {
    if (terminal == null or term_cols == 0 or term_rows == 0) return false;
    var sx0 = sel_x0;
    var sy0 = sel_y0;
    var sx1 = sel_x1;
    var sy1 = sel_y1;
    if (sy0 > sy1 or (sy0 == sy1 and sx0 > sx1)) {
        const tx = sx0;
        const ty = sy0;
        sx0 = sx1;
        sy0 = sy1;
        sx1 = tx;
        sy1 = ty;
    }
    if (sx0 >= term_cols) sx0 = term_cols - 1;
    if (sx1 >= term_cols) sx1 = term_cols - 1;
    if (sy0 >= term_rows) sy0 = term_rows - 1;
    if (sy1 >= term_rows) sy1 = term_rows - 1;
    var cap: usize = 512;
    var len: usize = 0;
    const out = c.malloc(cap);
    if (out == null) return false;
    @memset(@as([*]u8, @ptrCast(out))[0..cap], 0);
    var buf: [*c]u8 = @as([*c]u8, @ptrCast(out));
    var row: u32 = sy0;
    while (row <= sy1) : (row += 1) {
        const x_start: u16 = if (row == sy0) sx0 else 0;
        const x_end: u16 = if (row == sy1) sx1 else @as(u16, @intCast(term_cols - 1));

        const row_start_out = len;

        var col: u32 = x_start;
        while (col <= x_end) : (col += 1) {
            var point = std.mem.zeroes(c.GhosttyPoint);
            point.tag = c.GHOSTTY_POINT_TAG_VIEWPORT;
            point.value.coordinate.x = @as(u16, @intCast(col));
            point.value.coordinate.y = @as(u16, @intCast(row));
            var ref: c.GhosttyGridRef = undefined;
            @memset(@as([*]u8, @ptrCast(&ref))[0..@sizeOf(c.GhosttyGridRef)], 0);
            ref.size = @sizeOf(c.GhosttyGridRef);
            if (c.ghostty_terminal_grid_ref(terminal, point, &ref) != c.GHOSTTY_SUCCESS) {
                _ = appendBytes(&buf, &len, &cap, " ", 1);
                continue;
            }
            var cell: c.GhosttyCell = 0;
            if (c.ghostty_grid_ref_cell(&ref, &cell) != c.GHOSTTY_SUCCESS) {
                _ = appendBytes(&buf, &len, &cap, " ", 1);
                continue;
            }
            var wide: c.GhosttyCellWide = c.GHOSTTY_CELL_WIDE_NARROW;
            if (c.ghostty_cell_get(cell, c.GHOSTTY_CELL_DATA_WIDE, &wide) == c.GHOSTTY_SUCCESS and (wide == c.GHOSTTY_CELL_WIDE_SPACER_TAIL or wide == c.GHOSTTY_CELL_WIDE_SPACER_HEAD)) continue;
            var cps_inline: [16]u32 = undefined;
            var cp_len: usize = 0;
            const gr = c.ghostty_grid_ref_graphemes(&ref, &cps_inline, cps_inline.len, &cp_len);
            if (gr == c.GHOSTTY_OUT_OF_SPACE and cp_len > 0) {
                const dyn = c.malloc(cp_len * @sizeOf(u32));
                if (dyn == null) {
                    c.free(out);
                    return false;
                }
                const gr2 = c.ghostty_grid_ref_graphemes(&ref, @as([*c]u32, @ptrCast(@alignCast(dyn))), cp_len, &cp_len);
                if (gr2 == c.GHOSTTY_SUCCESS) {
                    for (@as([*]u32, @ptrCast(@alignCast(dyn)))[0..cp_len]) |cp| {
                        if (!appendUtf8Codepoint(&buf, &len, &cap, cp)) {
                            c.free(dyn);
                            c.free(out);
                            return false;
                        }
                    }
                } else {
                    if (!appendBytes(&buf, &len, &cap, " ", 1)) {
                        c.free(dyn);
                        c.free(out);
                        return false;
                    }
                }
                c.free(dyn);
                continue;
            }
            if (gr == c.GHOSTTY_SUCCESS and cp_len > 0) {
                for (cps_inline[0..cp_len]) |cp| {
                    if (!appendUtf8Codepoint(&buf, &len, &cap, cp)) {
                        c.free(out);
                        return false;
                    }
                }
            } else {
                if (!appendBytes(&buf, &len, &cap, " ", 1)) {
                    c.free(out);
                    return false;
                }
            }
        }

        while (len > row_start_out and buf[len - 1] == ' ') {
            len -= 1;
            buf[len] = 0;
        }

        if (row < sy1 and !viewportRowIsSoftWrapped(terminal, @as(u16, @intCast(row)))) {
            if (!appendBytes(&buf, &len, &cap, "\n", 1)) {
                c.free(out);
                return false;
            }
        }
    }
    c.SetClipboardText(buf);
    c.free(out);
    return true;
}

export fn paste_host_clipboard_to_terminal(pty_fd: c.PtyHandle, terminal: c.GhosttyTerminal) bool {
    const clip = c.GetClipboardText();
    if (clip == null or clip[0] == 0) return false;
    const clip_len = c.strlen(clip);
    var bracketed_paste = false;
    // Use raw value 2004 for GHOSTTY_MODE_BRACKETED_PASTE to avoid cimport type issue
    const mode = c.ghostty_mode_new(2004, false);
    if (c.ghostty_terminal_mode_get(terminal, mode, &bracketed_paste) == c.GHOSTTY_SUCCESS and bracketed_paste) {
        const paste_begin = "\x1b[200~";
        const paste_end = "\x1b[201~";
        c.pty_write(pty_fd, paste_begin, paste_begin.len - 1);
        c.pty_write(pty_fd, clip, clip_len);
        c.pty_write(pty_fd, paste_end, paste_end.len - 1);
    } else {
        c.pty_write(pty_fd, clip, clip_len);
    }
    return true;
}

export fn render_terminal(
    render_state: c.GhosttyRenderState,
    row_iter: c.GhosttyRenderStateRowIterator,
    cells: c.GhosttyRenderStateRowCells,
    font: c.Font,
    cell_width: c_int,
    cell_height: c_int,
    font_size: c_int,
    scrollbar: [*c]const c.GhosttyTerminalScrollbar,
    grid_origin_x: c_int,
    grid_origin_y: c_int,
    term_rows: u16,
    pad_right: c_int,
    selection_active: bool,
    sel_x0: u16,
    sel_y0: u16,
    sel_x1: u16,
    sel_y1: u16,
    current_han_tier: c.GhostlingHanTier,
) c.GhostlingHanTier {
    var missing_han_tier: c.GhostlingHanTier = c.GHOSTLING_HAN_TIER_NONE;

    var colors = std.mem.zeroes(c.GhosttyRenderStateColors);
    colors.size = @sizeOf(c.GhosttyRenderStateColors);
    if (c.ghostty_render_state_colors_get(render_state, &colors) != c.GHOSTTY_SUCCESS)
        return c.GHOSTLING_HAN_TIER_NONE;

    if (c.ghostty_render_state_get(render_state, c.GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR, @ptrCast(@constCast(&row_iter))) != c.GHOSTTY_SUCCESS)
        return c.GHOSTLING_HAN_TIER_NONE;

    var y = grid_origin_y;
    var row_idx: u16 = 0;
    const selection_bg = if (colors.cursor_has_value) c.Color{ .r = colors.cursor.r, .g = colors.cursor.g, .b = colors.cursor.b, .a = 96 } else c.Color{ .r = 96, .g = 128, .b = 192, .a = 96 };

    while (c.ghostty_render_state_row_iterator_next(row_iter)) {
        var row_raw: c.GhosttyRow = 0;
        var row_prompt: c.GhosttyRowSemanticPrompt = c.GHOSTTY_ROW_SEMANTIC_NONE;
        if (c.ghostty_render_state_row_get(row_iter, c.GHOSTTY_RENDER_STATE_ROW_DATA_RAW, &row_raw) == c.GHOSTTY_SUCCESS)
            _ = c.ghostty_row_get(row_raw, c.GHOSTTY_ROW_DATA_SEMANTIC_PROMPT, &row_prompt);

        if (c.ghostty_render_state_row_get(row_iter, c.GHOSTTY_RENDER_STATE_ROW_DATA_CELLS, @ptrCast(@constCast(&cells))) != c.GHOSTTY_SUCCESS)
            continue;

        var cols_in_row: c_int = 0;
        var x = grid_origin_x;

        // First pass: draw backgrounds
        while (c.ghostty_render_state_row_cells_next(cells)) {
            const col_idx: u16 = @intCast(cols_in_row);
            var bg_rgb = colors.background;
            var has_bg = c.ghostty_render_state_row_cells_get(cells, c.GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_BG_COLOR, &bg_rgb) == c.GHOSTTY_SUCCESS;

            var style = std.mem.zeroes(c.GhosttyStyle);
            style.size = @sizeOf(c.GhosttyStyle);
            _ = c.ghostty_render_state_row_cells_get(cells, c.GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_STYLE, &style);

            if (style.inverse) {
                var fg = colors.foreground;
                _ = c.ghostty_render_state_row_cells_get(cells, c.GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_FG_COLOR, &fg);
                bg_rgb = fg;
                has_bg = true;
            }

            if (has_bg) {
                c.DrawRectangle(x, y, cell_width, cell_height, .{ .r = bg_rgb.r, .g = bg_rgb.g, .b = bg_rgb.b, .a = 255 });
            }

            if (selectionContainsCell(selection_active, sel_x0, sel_y0, sel_x1, sel_y1, col_idx, row_idx)) {
                c.DrawRectangle(x, y, cell_width, cell_height, selection_bg);
            }

            x += cell_width;
            cols_in_row += 1;
        }

        // Second pass: draw glyphs
        var col: c_int = 0;
        while (col < cols_in_row) : (col += 1) {
            if (c.ghostty_render_state_row_cells_select(cells, @as(u16, @intCast(col))) != c.GHOSTTY_SUCCESS)
                continue;

            var grapheme_len: u32 = 0;
            _ = c.ghostty_render_state_row_cells_get(cells, c.GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_LEN, &grapheme_len);
            if (grapheme_len == 0) continue;

            var codepoints: [16]u32 = undefined;
            const len = if (grapheme_len < 16) grapheme_len else 16;
            _ = c.ghostty_render_state_row_cells_get(cells, c.GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_BUF, &codepoints);

            var text: [64]u8 = std.mem.zeroes([64]u8);
            var pos: usize = 0;
            var i: u32 = 0;
            while (i < len and pos < 60) : (i += 1) {
                if (current_han_tier != c.GHOSTLING_HAN_TIER_NONE and current_han_tier != c.GHOSTLING_HAN_TIER_8105) {
                    const cp_tier = c.ghostling_han_tier_for_codepoint(codepoints[i]);
                    if (cp_tier > current_han_tier and cp_tier > missing_han_tier)
                        missing_han_tier = cp_tier;
                }
                var u8_buf: [4]u8 = undefined;
                // Pass pointer to first element of u8_buf
                const n = utf8Encode(codepoints[i], @as([*c]u8, @ptrCast(&u8_buf[0])));
                @memcpy(text[pos .. pos + @as(usize, @intCast(n))], u8_buf[0..@as(usize, @intCast(n))]);
                pos += @as(usize, @intCast(n));
            }
            text[pos] = 0;

            var fg = colors.foreground;
            _ = c.ghostty_render_state_row_cells_get(cells, c.GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_FG_COLOR, &fg);

            var bg_rgb = colors.background;
            _ = c.ghostty_render_state_row_cells_get(cells, c.GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_BG_COLOR, &bg_rgb);

            var style = std.mem.zeroes(c.GhosttyStyle);
            style.size = @sizeOf(c.GhosttyStyle);
            _ = c.ghostty_render_state_row_cells_get(cells, c.GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_STYLE, &style);

            if (style.inverse) {
                const tmp = fg;
                fg = bg_rgb;
                bg_rgb = tmp;
            }

            const ray_fg = c.Color{ .r = fg.r, .g = fg.g, .b = fg.b, .a = 255 };
            const italic_offset = if (style.italic) @as(c_int, @intCast(@as(u32, @intCast(font_size)) / 6)) else @as(c_int, 0);
            const draw_x = grid_origin_x + col * cell_width;

            const text_ptr: [*c]const u8 = @ptrCast(&text[0]);
            c.DrawTextEx(font, text_ptr, .{ .x = @as(f32, @floatFromInt(draw_x + italic_offset)), .y = @as(f32, @floatFromInt(y)) }, @as(f32, @floatFromInt(font_size)), 0, ray_fg);

            if (style.bold) {
                c.DrawTextEx(font, text_ptr, .{ .x = @as(f32, @floatFromInt(draw_x + italic_offset + 1)), .y = @as(f32, @floatFromInt(y)) }, @as(f32, @floatFromInt(font_size)), 0, ray_fg);
            }
        }

        if (row_prompt != c.GHOSTTY_ROW_SEMANTIC_NONE) {
            const marker = if (row_prompt == c.GHOSTTY_ROW_SEMANTIC_PROMPT) c.Color{ .r = 80, .g = 170, .b = 120, .a = 200 } else c.Color{ .r = 80, .g = 140, .b = 170, .a = 180 };
            c.DrawRectangle(grid_origin_x, y, 2, cell_height, marker);
        }

        var clean = false;
        _ = c.ghostty_render_state_row_set(row_iter, c.GHOSTTY_RENDER_STATE_ROW_OPTION_DIRTY, &clean);

        y += cell_height;
        row_idx += 1;
    }

    // Draw cursor
    var cursor_visible = false;
    _ = c.ghostty_render_state_get(render_state, c.GHOSTTY_RENDER_STATE_DATA_CURSOR_VISIBLE, &cursor_visible);
    var cursor_in_viewport = false;
    _ = c.ghostty_render_state_get(render_state, c.GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_HAS_VALUE, &cursor_in_viewport);

    if (cursor_visible and cursor_in_viewport) {
        var cx: u16 = 0;
        var cy: u16 = 0;
        _ = c.ghostty_render_state_get(render_state, c.GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_X, &cx);
        _ = c.ghostty_render_state_get(render_state, c.GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_Y, &cy);

        var cur_rgb = colors.foreground;
        if (colors.cursor_has_value)
            cur_rgb = colors.cursor;
        const cur_x = grid_origin_x + @as(c_int, @intCast(cx)) * cell_width;
        const cur_y = grid_origin_y + @as(c_int, @intCast(cy)) * cell_height;
        c.DrawRectangle(cur_x, cur_y, cell_width, cell_height, .{ .r = cur_rgb.r, .g = cur_rgb.g, .b = cur_rgb.b, .a = 128 });
    }

    // Draw scrollbar
    if (scrollbar != null and scrollbar.*.total > scrollbar.*.len) {
        const scr_w = c.GetScreenWidth();
        var track_h = @as(c_int, @intCast(term_rows)) * cell_height;
        if (track_h < 1) track_h = 1;

        const bar_width: c_int = 6;
        const bar_margin: c_int = 2;
        const bar_x = scr_w - pad_right - bar_width - bar_margin;

        const visible_frac = @as(f64, @floatFromInt(scrollbar.*.len)) / @as(f64, @floatFromInt(scrollbar.*.total));
        var thumb_height = @as(c_int, @intFromFloat(@as(f64, @floatFromInt(track_h)) * visible_frac));
        if (thumb_height < 10) thumb_height = 10;
        if (thumb_height > track_h) thumb_height = track_h;

        const scroll_frac = if (scrollbar.*.total > scrollbar.*.len) @as(f64, @floatFromInt(scrollbar.*.offset)) / @as(f64, @floatFromInt(scrollbar.*.total - scrollbar.*.len)) else 1.0;
        const thumb_y = grid_origin_y + @as(c_int, @intFromFloat(scroll_frac * @as(f64, @floatFromInt(track_h - thumb_height))));

        c.DrawRectangle(bar_x, thumb_y, bar_width, thumb_height, .{ .r = 200, .g = 200, .b = 200, .a = 128 });
    }

    var clean_state: c.GhosttyRenderStateDirty = c.GHOSTTY_RENDER_STATE_DIRTY_FALSE;
    _ = c.ghostty_render_state_set(render_state, c.GHOSTTY_RENDER_STATE_OPTION_DIRTY, &clean_state);

    return missing_han_tier;
}

export fn log_build_info() void {
    var simd = false;
    _ = c.ghostty_build_info(c.GHOSTTY_BUILD_INFO_SIMD, &simd);

    var opt: c.GhosttyOptimizeMode = c.GHOSTTY_OPTIMIZE_DEBUG;
    _ = c.ghostty_build_info(c.GHOSTTY_BUILD_INFO_OPTIMIZE, &opt);

    const opt_str = switch (opt) {
        c.GHOSTTY_OPTIMIZE_DEBUG => "Debug",
        c.GHOSTTY_OPTIMIZE_RELEASE_SAFE => "ReleaseSafe",
        c.GHOSTTY_OPTIMIZE_RELEASE_SMALL => "ReleaseSmall",
        c.GHOSTTY_OPTIMIZE_RELEASE_FAST => "ReleaseFast",
        else => "Unknown",
    };

    c.TraceLog(c.LOG_INFO, "ghostty-vt: simd:     %s", if (simd) @as([*c]const u8, @ptrCast("enabled")) else @as([*c]const u8, @ptrCast("disabled")));
    c.TraceLog(c.LOG_INFO, "ghostty-vt: optimize: %s", @as([*c]const u8, @ptrCast(opt_str)));
}
