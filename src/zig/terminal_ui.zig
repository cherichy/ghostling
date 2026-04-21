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
    const n = utf8Encode(cp, @as([*c]u8, @ptrCast(&u8_buf)));
    return appendBytes(buf, len, cap, @as([*c]const u8, @ptrCast(&u8_buf)), @as(usize, @intCast(n)));
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
    while (end.* != 0 and c.isspace(@as(c_uint, @bitCast(end.*))) == 0) end += 1;
    while (end > actual_scheme and c.strchr(")]}'\".,;:!?", end[-1]) != null) end -= 1;
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
    const ok = scanFirstUrlInText(buf, &url, url.len);
    c.free(row_text);
    if (!ok) return false;
    if (builtin.os.tag == .macos) {
        var pid: c.pid_t = 0;
        var argv: [3]?[*:0]const u8 = .{ "open", @as([*:0]const u8, @ptrCast(&url)), null };
        return c.posix_spawnp(&pid, "open", null, null, @as([*c]?[*:0]const u8, @ptrCast(&argv)), @extern([*c]?[*:0]const u8, .{ .name = "environ" })) == 0;
    }
    return false;
}

export fn handle_mouse(pty_fd: c.PtyHandle, encoder: c.GhosttyMouseEncoder, event: c.GhosttyMouseEvent, terminal: c.GhosttyTerminal, cell_width: c_int, cell_height: c_int, pad_left: c_int, pad_top: c_int, pad_right: c_int, pad_bottom: c_int, screen_width: c_int, screen_height: c_int) bool {
    _ = pad_right;
    _ = pad_bottom;
    _ = terminal;
    _ = screen_width;
    _ = screen_height;
    if (c.IsMouseButtonPressed(c.MOUSE_BUTTON_LEFT)) {
        const mpos = c.GetMousePosition();
        const col = @as(u16, @intCast(@max(0, @divTrunc(@as(c_int, @intFromFloat(mpos.x)) - pad_left, cell_width))));
        const row = @as(u16, @intCast(@max(0, @divTrunc(@as(c_int, @intFromFloat(mpos.y)) - pad_top, cell_height))));
        _ = col;
        _ = row;
    }
    if (c.GetMouseWheelMove() != 0) {
        const mods = ghosttyModsFromRaylib();
        const wheel_delta = c.GetMouseWheelMove();
        _ = c.ghostty_mouse_event_set_wheel(event, if (wheel_delta > 0) c.GHOSTTY_MOUSE_WHEEL_UP else c.GHOSTTY_MOUSE_WHEEL_DOWN, @as(u32, @intCast(@abs(wheel_delta))));
        _ = c.ghostty_mouse_event_set_mods(event, mods);
        _ = c.ghostty_mouse_event_set_action(event, c.GHOSTTY_MOUSE_ACTION_PRESS);
        var buf: [64]u8 = undefined;
        var written: usize = 0;
        const res = c.ghostty_mouse_encoder_setopt(encoder, event, @as([*c]u8, @ptrCast(&buf)), buf.len, &written);
        if (res == c.GHOSTTY_SUCCESS and written > 0) {
            var remaining: isize = @as(isize, @intCast(written));
            var ptr = @as([*c]const u8, @ptrCast(&buf));
            while (remaining > 0) {
                const n = c.write(pty_fd, ptr, @as(usize, @intCast(remaining)));
                if (n > 0) {
                    ptr += @as(usize, @intCast(n));
                    remaining -= n;
                } else if (n < 0 and c.__error().* == c.EINTR) {
                    continue;
                } else {
                    break;
                }
            }
        }
        return true;
    }
    return false;
}

export fn handle_input(pty_fd: c.PtyHandle, encoder: c.GhosttyKeyEncoder, event: c.GhosttyKeyEvent, terminal: c.GhosttyTerminal) bool {
    _ = terminal;
    const rl_key = c.GetKeyPressed();
    if (rl_key == 0) return false;
    const mods = ghosttyModsFromRaylib();
    if (encoder == null or event == null) {
        var buf: [64]u8 = undefined;
        var written: usize = 0;
        if (rl_key >= 32 and rl_key <= 126) {
            buf[0] = @as(u8, @intCast(rl_key));
            written = 1;
        } else {
            const mapped = raylibKeyToGhostty(rl_key);
            if (mapped == c.GHOSTTY_KEY_UNIDENTIFIED) return false;
            _ = c.ghostty_key_event_set_key(event, mapped);
            _ = c.ghostty_key_event_set_mods(event, mods);
            _ = c.ghostty_key_event_set_action(event, c.GHOSTTY_KEY_ACTION_PRESS);
            if (c.ghostty_key_encoder_setopt(encoder, event, &buf, buf.len, &written) != c.GHOSTTY_SUCCESS) return false;
        }
        if (written > 0) {
            var remaining: isize = @as(isize, @intCast(written));
            var ptr = @as([*c]const u8, @ptrCast(&buf));
            while (remaining > 0) {
                const n = c.write(pty_fd, ptr, @as(usize, @intCast(remaining)));
                if (n > 0) {
                    ptr += @as(usize, @intCast(n));
                    remaining -= n;
                } else if (n < 0 and c.__error().* == c.EINTR) {
                    continue;
                } else {
                    break;
                }
            }
        }
        return true;
    }
    return false;
}

fn raylibKeyToGhostty(rl_key: c_int) c.GhosttyKey {
    if (rl_key >= 65 and rl_key <= 90) return @as(c.GhosttyKey, @intCast(@as(c_uint, @intCast(c.GHOSTTY_KEY_A)) + @as(c_uint, @intCast(rl_key - 65))));
    if (rl_key >= 48 and rl_key <= 57) return @as(c.GhosttyKey, @intCast(@as(c_uint, @intCast(c.GHOSTTY_KEY_DIGIT_0)) + @as(c_uint, @intCast(rl_key - 48))));
    if (rl_key >= 290 and rl_key <= 301) return @as(c.GhosttyKey, @intCast(@as(c_uint, @intCast(c.GHOSTTY_KEY_F1)) + @as(c_uint, @intCast(rl_key - 290))));
    return switch (rl_key) {
        32 => c.GHOSTTY_KEY_SPACE,
        257 => c.GHOSTTY_KEY_ENTER,
        258 => c.GHOSTTY_KEY_TAB,
        259 => c.GHOSTTY_KEY_BACKSPACE,
        261 => c.GHOSTTY_KEY_DELETE,
        256 => c.GHOSTTY_KEY_ESCAPE,
        265 => c.GHOSTTY_KEY_ARROW_UP,
        264 => c.GHOSTTY_KEY_ARROW_DOWN,
        263 => c.GHOSTTY_KEY_ARROW_LEFT,
        262 => c.GHOSTTY_KEY_ARROW_RIGHT,
        268 => c.GHOSTTY_KEY_HOME,
        269 => c.GHOSTTY_KEY_END,
        266 => c.GHOSTTY_KEY_PAGE_UP,
        267 => c.GHOSTTY_KEY_PAGE_DOWN,
        260 => c.GHOSTTY_KEY_INSERT,
        45 => c.GHOSTTY_KEY_MINUS,
        61 => c.GHOSTTY_KEY_EQUAL,
        91 => c.GHOSTTY_KEY_BRACKET_LEFT,
        93 => c.GHOSTTY_KEY_BRACKET_RIGHT,
        92 => c.GHOSTTY_KEY_BACKSLASH,
        59 => c.GHOSTTY_KEY_SEMICOLON,
        39 => c.GHOSTTY_KEY_QUOTE,
        44 => c.GHOSTTY_KEY_COMMA,
        46 => c.GHOSTTY_KEY_PERIOD,
        47 => c.GHOSTTY_KEY_SLASH,
        96 => c.GHOSTTY_KEY_BACKQUOTE,
        else => c.GHOSTTY_KEY_UNIDENTIFIED,
    };
}

export fn handle_scrollbar(terminal: c.GhosttyTerminal, render_state: c.GhosttyRenderState, dragging: *bool, grid_origin_x: c_int, grid_origin_y: c_int, term_rows: u16, cell_height: c_int, pad_right: c_int) bool {
    _ = terminal;
    _ = render_state;
    _ = dragging;
    _ = grid_origin_x;
    _ = grid_origin_y;
    _ = term_rows;
    _ = cell_height;
    _ = pad_right;
    return false;
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
        if (row < sy1) {
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
    _ = terminal;
    const clip = c.GetClipboardText();
    if (clip == null or clip[0] == 0) return false;
    const len = c.strlen(clip);
    var remaining: isize = @as(isize, @intCast(len));
    var ptr = clip;
    while (remaining > 0) {
        const n = c.write(pty_fd, ptr, @as(usize, @intCast(remaining)));
        if (n > 0) {
            ptr += @as(usize, @intCast(n));
            remaining -= n;
        } else if (n < 0 and c.__error().* == c.EINTR) {
            continue;
        } else {
            break;
        }
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
    _ = render_state;
    _ = row_iter;
    _ = cells;
    _ = font;
    _ = cell_width;
    _ = cell_height;
    _ = font_size;
    _ = scrollbar;
    _ = grid_origin_x;
    _ = grid_origin_y;
    _ = term_rows;
    _ = pad_right;
    _ = selection_active;
    _ = sel_x0;
    _ = sel_y0;
    _ = sel_x1;
    _ = sel_y1;
    _ = current_han_tier;
    return 0;
}
