const std = @import("std");

const rl = @import("c").c;

pub const ToggleW: c_int = 22;
pub const ToggleH: c_int = 28;
const TAB_NEW_H: c_int = 44;
const TAB_CLOSE_W: c_int = 28;
const TAB_INDEX_COL_W: c_int = 28;

const TabPalette = struct {
    strip_bg: rl.Color = .{ .r = 45, .g = 45, .b = 48, .a = 255 },
    index_bg: rl.Color = .{ .r = 40, .g = 40, .b = 44, .a = 255 },
    reserved_bg: rl.Color = .{ .r = 38, .g = 38, .b = 42, .a = 255 },
    tab_bg: rl.Color = .{ .r = 55, .g = 55, .b = 58, .a = 255 },
    active_bg: rl.Color = .{ .r = 70, .g = 100, .b = 140, .a = 255 },
    border: rl.Color = .{ .r = 80, .g = 80, .b = 85, .a = 255 },
    fg: rl.Color = .{ .r = 220, .g = 220, .b = 220, .a = 255 },
    edit_bg: rl.Color = .{ .r = 50, .g = 70, .b = 95, .a = 255 },
};

pub const palette = TabPalette{};

fn clampScale(scale: f32) f32 {
    return if (scale > 0.0) scale else 1.0;
}

fn snapToPhysical(value: f32, scale: f32) f32 {
    const s = clampScale(scale);
    const scaled = value * s;
    const snapped = if (scaled >= 0.0) @floor(scaled + 0.5) else @ceil(scaled - 0.5);
    return snapped / s;
}

fn quantizeFontSize(font_size: f32, dpi_y: f32) f32 {
    return snapToPhysical(font_size, dpi_y);
}

fn currentDpiScale() rl.Vector2 {
    var dpi = rl.GetWindowScaleDPI();
    dpi.x = clampScale(dpi.x);
    dpi.y = clampScale(dpi.y);
    return dpi;
}

fn drawTextSyntheticBold(font: rl.Font, text: [*c]const u8, pos: rl.Vector2, font_size: f32, fg: rl.Color, dpi_scale: rl.Vector2) void {
    const dx = 1.0 / clampScale(dpi_scale.x);
    const dy = 1.0 / clampScale(dpi_scale.y);
    const p = rl.Vector2{
        .x = snapToPhysical(pos.x, dpi_scale.x),
        .y = snapToPhysical(pos.y, dpi_scale.y),
    };
    var embolden = fg;
    embolden.a = @as(u8, @intCast(@as(u32, @intCast(fg.a)) * 170 / 255));
    rl.DrawTextEx(font, text, .{ .x = snapToPhysical(p.x + dx, dpi_scale.x), .y = p.y }, font_size, 0, embolden);
    rl.DrawTextEx(font, text, .{ .x = p.x, .y = snapToPhysical(p.y + dy, dpi_scale.y) }, font_size, 0, embolden);
    rl.DrawTextEx(font, text, .{ .x = snapToPhysical(p.x + dx, dpi_scale.x), .y = snapToPhysical(p.y + dy, dpi_scale.y) }, font_size, 0, embolden);
    rl.DrawTextEx(font, text, p, font_size, 0, fg);
}

fn drawText(font: *const rl.Font, text: [*c]const u8, pos: rl.Vector2, font_size: f32, color: rl.Color) void {
    rl.DrawTextEx(font.*, text, pos, font_size, 0, color);
}

fn measureText(font: *const rl.Font, text: [*c]const u8, font_size: f32) rl.Vector2 {
    return rl.MeasureTextEx(font.*, text, font_size, 0);
}

fn truncateToWidth(font: *const rl.Font, font_size: f32, src: [*c]const u8, max_w: f32, out: [*c]u8, outsz: usize) void {
    if (max_w < 8.0) {
        if (outsz > 0) out[0] = 0;
        return;
    }
    if (measureText(font, src, font_size).x <= max_w) {
        _ = rl.snprintf(out, outsz, "%s", src);
        return;
    }
    var tmp: [512]u8 = undefined;
    _ = rl.snprintf(@as([*c]u8, @ptrCast(&tmp[0])), tmp.len, "%s", src);
    var n = std.mem.indexOfScalar(u8, &tmp, 0) orelse tmp.len;
    while (n > 0) {
        n -= 1;
        tmp[n] = 0;
        if (measureText(font, @as([*c]const u8, @ptrCast(&tmp[0])), font_size).x <= max_w) {
            _ = rl.snprintf(out, outsz, "%s", @as([*c]const u8, @ptrCast(&tmp[0])));
            return;
        }
    }
    if (outsz > 0) out[0] = 0;
}

fn iconFontSize(base_font: f32) f32 {
    var size = base_font * 1.3;
    if (size < base_font + 2.0) size = base_font + 2.0;
    return size;
}

fn splitterToggleBounds(effective_strip_w: c_int, scr_h: c_int, tx: *c_int, ty: *c_int, tw: *c_int, th: *c_int) void {
    tw.* = ToggleW;
    th.* = ToggleH;
    tx.* = effective_strip_w - tw.* - 1;
    ty.* = @divTrunc(scr_h, 2) - @divTrunc(th.*, 2);
}

fn collapsedExpandBounds(scr_h: c_int, tx: *c_int, ty: *c_int, tw: *c_int, th: *c_int) void {
    tw.* = ToggleW;
    th.* = ToggleH;
    tx.* = @divTrunc(20 - tw.*, 2);
    if (tx.* < 0) tx.* = 0;
    ty.* = @divTrunc(scr_h, 2) - @divTrunc(th.*, 2);
}

pub fn tab_splitter_hit(mpos: rl.Vector2, strip_w: c_int, scr_h: c_int) bool {
    _ = scr_h;
    return mpos.x >= @as(f32, @floatFromInt(strip_w)) and
        mpos.x <= @as(f32, @floatFromInt(strip_w + 8));
}

pub fn tab_splitter_toggle_hit(mpos: rl.Vector2, effective_strip_w: c_int, scr_h: c_int) bool {
    if (effective_strip_w == 0) {
        var tx: c_int = 0;
        var ty: c_int = 0;
        var tw: c_int = 0;
        var th: c_int = 0;
        collapsedExpandBounds(scr_h, &tx, &ty, &tw, &th);
        return mpos.x >= @as(f32, @floatFromInt(tx)) and mpos.x < @as(f32, @floatFromInt(tx + tw)) and
            mpos.y >= @as(f32, @floatFromInt(ty)) and mpos.y < @as(f32, @floatFromInt(ty + th));
    }
    var tx: c_int = 0;
    var ty: c_int = 0;
    var tw: c_int = 0;
    var th: c_int = 0;
    splitterToggleBounds(effective_strip_w, scr_h, &tx, &ty, &tw, &th);
    return mpos.x >= @as(f32, @floatFromInt(tx)) and mpos.x < @as(f32, @floatFromInt(tx + tw)) and
        mpos.y >= @as(f32, @floatFromInt(ty)) and mpos.y < @as(f32, @floatFromInt(ty + th));
}

pub fn tab_strip_hit(
    mpos: rl.Vector2,
    strip_w: c_int,
    scr_h: c_int,
    n_tabs: usize,
    idx: *usize,
    act: *c_int,
    tab_title_h: c_int,
    tab_reserved_h: c_int,
    strip_collapsed: bool,
) bool {
    if (strip_collapsed) return false;
    if (tab_splitter_toggle_hit(mpos, strip_w, scr_h)) return false;
    const title_h = if (tab_title_h < 1) 1 else tab_title_h;
    const reserved_h = if (tab_reserved_h < 0) 0 else tab_reserved_h;
    const row_h = title_h + reserved_h;
    if (row_h < 1) return false;

    act.* = 0;
    if (mpos.x < 0 or mpos.x >= @as(f32, @floatFromInt(strip_w))) return false;

    const new_y0 = scr_h - TAB_NEW_H;
    if (mpos.y >= @as(f32, @floatFromInt(new_y0))) {
        act.* = 2;
        return true;
    }

    const row = @divTrunc(@as(c_int, @intFromFloat(mpos.y)), row_h);
    if (row < 0) {
        act.* = 0;
        return true;
    }

    const max_vis = if (new_y0 > 0) @as(usize, @intCast(@divTrunc(new_y0, row_h))) else 1;
    if (max_vis == 0) return false;

    if (@as(usize, @intCast(row)) >= n_tabs or @as(usize, @intCast(row)) >= max_vis) {
        act.* = 0;
        return true;
    }

    idx.* = @as(usize, @intCast(row));
    const y_in_tab = @as(c_int, @intFromFloat(mpos.y)) - row * row_h;
    const in_title = y_in_tab < title_h;
    if (mpos.x >= @as(f32, @floatFromInt(strip_w - TAB_CLOSE_W)) and in_title) {
        act.* = 1;
    } else {
        act.* = 0;
    }
    return true;
}

pub fn tab_strip_draw(
    font: *const rl.Font,
    font_size: f32,
    strip_w: c_int,
    scr_h: c_int,
    tabs: [*c]const [*c]rl.Tab,
    n_tabs: usize,
    active_idx: usize,
    edit_idx: usize,
    edit_buf: [*c]const u8,
    tab_title_h: c_int,
    tab_reserved_h: c_int,
    strip_collapsed: bool,
) void {
    if (strip_collapsed) return;

    // Keep an unmistakable base layer so tab strip rendering failures are visible.
    // This also avoids a visually "black" strip when per-tab drawing is skipped.
    rl.DrawRectangle(0, 0, strip_w, scr_h, palette.strip_bg);
    rl.DrawRectangle(strip_w - 1, 0, 1, scr_h, palette.border);

    const dpi_scale = currentDpiScale();
    const text_font = if (font_size > 0.0) font_size else 12.0;

    const title_h = if (tab_title_h < 1) 1 else tab_title_h;
    const reserved_h = if (tab_reserved_h < 0) 0 else tab_reserved_h;
    const row_h = title_h + reserved_h;
    if (row_h < 1) return;

    var ix: c_int = TAB_INDEX_COL_W;
    if (ix >= strip_w - @as(c_int, @intCast(TAB_CLOSE_W)) - 8)
        ix = if (strip_w > 40) 20 else 0;

    const new_y0 = scr_h - TAB_NEW_H;
    var max_vis: usize = if (new_y0 > 0) @intCast(@divTrunc(new_y0, row_h)) else 1;
    if (max_vis == 0) max_vis = 1;

    const label_max_w: f32 = @as(f32, @floatFromInt(strip_w - ix - @as(c_int, @intCast(TAB_CLOSE_W)) - 10));
    const label_max_w_clamped = if (label_max_w < 20.0) 20.0 else label_max_w;

    var i: usize = 0;
    while (i < n_tabs and i < max_vis) : (i += 1) {
        const y0 = @as(c_int, @intCast(i * @as(usize, @intCast(row_h))));
        const editing = (edit_idx == i);
        const tab_ptr = tabs[i];
        if (tab_ptr == null) continue;

        rl.DrawRectangle(0, y0, ix, row_h, palette.index_bg);
        rl.DrawRectangle(ix - 1, y0, 1, row_h, palette.border);

        const title_bg = if (editing) palette.edit_bg else if (i == active_idx) palette.active_bg else palette.tab_bg;
        rl.DrawRectangle(ix, y0, strip_w - ix - 1, title_h, title_bg);

        const y_res = y0 + title_h;
        rl.DrawRectangle(ix, y_res, strip_w - ix - 1, reserved_h, palette.reserved_bg);
        rl.DrawRectangle(ix, y0 + title_h - 1, strip_w - ix - 1, 1, palette.border);
        rl.DrawRectangle(0, y_res + reserved_h - 1, strip_w - 1, 1, palette.border);

        // Tab index number
        var num_buf: [8]u8 = std.mem.zeroes([8]u8);
        _ = rl.snprintf(@as([*c]u8, @ptrCast(&num_buf[0])), num_buf.len, "%zu", i + 1);
        const num_ptr: [*c]const u8 = @ptrCast(&num_buf[0]);
        const ns = measureText(font, num_ptr, text_font);
        var nx = (@as(f32, @floatFromInt(ix)) - ns.x) * 0.5;
        if (nx < 2.0) nx = 2.0;
        const ny = @as(f32, @floatFromInt(y0)) + (@as(f32, @floatFromInt(row_h)) - ns.y) * 0.5;
        const num_pos = rl.Vector2{ .x = snapToPhysical(nx, dpi_scale.x), .y = snapToPhysical(ny, dpi_scale.y) };
        if (i == active_idx) {
            drawTextSyntheticBold(font.*, num_ptr, num_pos, text_font, palette.fg, dpi_scale);
        } else {
            drawText(font, num_ptr, num_pos, text_font, palette.fg);
        }

        // Tab title
        var line: [512]u8 = std.mem.zeroes([512]u8);
        if (editing and edit_buf != null) {
            _ = rl.snprintf(@as([*c]u8, @ptrCast(&line[0])), line.len, "%s", edit_buf);
        } else {
            var raw: [256]u8 = std.mem.zeroes([256]u8);
            rl.tab_display_title(tab_ptr, i + 1, @as([*c]u8, @ptrCast(&raw[0])), raw.len);
            truncateToWidth(font, text_font, @as([*c]const u8, @ptrCast(&raw[0])), label_max_w_clamped, @as([*c]u8, @ptrCast(&line[0])), line.len);
        }

        const line_ptr: [*c]const u8 = @ptrCast(&line[0]);
        const ts = measureText(font, line_ptr, text_font);
        const tx = @as(f32, @floatFromInt(ix)) + 6.0;
        const ty = @as(f32, @floatFromInt(y0)) + (@as(f32, @floatFromInt(title_h)) - ts.y) * 0.5;
        const title_pos = rl.Vector2{ .x = snapToPhysical(tx, dpi_scale.x), .y = snapToPhysical(ty, dpi_scale.y) };
        drawText(font, line_ptr, title_pos, text_font, palette.fg);

        // Agent status in reserved area
        if (reserved_h > 0) {
            var status_raw: [96]u8 = std.mem.zeroes([96]u8);
            const agent_state = &tab_ptr.*.agent_state;
            const agent_name = rl.ghostling_agent_state_agent(agent_state);
            if (agent_name != null and agent_name[0] != 0) {
                _ = rl.snprintf(@as([*c]u8, @ptrCast(&status_raw[0])), status_raw.len, "[%s]: %s", agent_name, rl.ghostling_agent_state_label(agent_state));
            } else {
                _ = rl.snprintf(@as([*c]u8, @ptrCast(&status_raw[0])), status_raw.len, "%s", rl.ghostling_agent_state_label(agent_state));
            }

            var status_line: [96]u8 = std.mem.zeroes([96]u8);
            truncateToWidth(font, text_font, @as([*c]const u8, @ptrCast(&status_raw[0])), label_max_w_clamped, @as([*c]u8, @ptrCast(&status_line[0])), status_line.len);

            const status_ptr: [*c]const u8 = @ptrCast(&status_line[0]);
            const ss = measureText(font, status_ptr, text_font);
            const sy = @as(f32, @floatFromInt(y_res)) + (@as(f32, @floatFromInt(reserved_h)) - ss.y) * 0.5;
            const status_pos = rl.Vector2{ .x = snapToPhysical(tx, dpi_scale.x), .y = snapToPhysical(sy, dpi_scale.y) };
            drawText(font, status_ptr, status_pos, text_font, palette.fg);
        }

        // Close button
        const x_str: [*c]const u8 = "x";
        const x_font_size: c_int = @max(8, @as(c_int, @intFromFloat(text_font)));
        const xs = measureText(font, x_str, @floatFromInt(x_font_size));
        const cx = @as(f32, @floatFromInt(strip_w - @as(c_int, @intCast(TAB_CLOSE_W)))) + ((@as(f32, @floatFromInt(@as(c_int, @intCast(TAB_CLOSE_W)))) - xs.x) * 0.5);
        const close_x = @as(c_int, @intFromFloat(snapToPhysical(cx, dpi_scale.x)));
        const close_y = @as(c_int, @intFromFloat(snapToPhysical(@as(f32, @floatFromInt(y0)) + (@as(f32, @floatFromInt(title_h)) - xs.y) * 0.5, dpi_scale.y)));
        drawText(font, x_str, .{ .x = @floatFromInt(close_x), .y = @floatFromInt(close_y) }, @floatFromInt(x_font_size), palette.fg);
    }

    // New tab button
    rl.DrawRectangle(0, new_y0, strip_w - 1, TAB_NEW_H, palette.tab_bg);
    rl.DrawRectangle(0, new_y0, strip_w - 1, 1, palette.border);
    const plus: [*c]const u8 = "+";
    const icon_font = quantizeFontSize(iconFontSize(text_font), dpi_scale.y);
    const ps = measureText(font, plus, icon_font);
    const plus_pos = rl.Vector2{
        .x = snapToPhysical((@as(f32, @floatFromInt(strip_w)) - ps.x) * 0.5, dpi_scale.x),
        .y = snapToPhysical(@as(f32, @floatFromInt(new_y0)) + (@as(f32, @floatFromInt(TAB_NEW_H)) - ps.y) * 0.5, dpi_scale.y),
    };
    drawTextSyntheticBold(font.*, plus, plus_pos, icon_font, palette.fg, dpi_scale);
}

pub fn tab_splitter_toggle_draw(
    font: *const rl.Font,
    font_size: f32,
    strip_w: c_int,
    scr_h: c_int,
    strip_collapsed: bool,
    show: bool,
    fg: rl.Color,
) void {
    if (!show) return;
    const dpi_scale = currentDpiScale();
    const icon_font = quantizeFontSize(iconFontSize(quantizeFontSize(font_size, dpi_scale.y)), dpi_scale.y);
    if (strip_collapsed) {
        var tx: c_int = 0;
        var ty: c_int = 0;
        var tw: c_int = 0;
        var th: c_int = 0;
        collapsedExpandBounds(scr_h, &tx, &ty, &tw, &th);
        const ch: [*c]const u8 = ">";
        const cs = measureText(font, ch, icon_font);
        drawText(font, ch, .{
            .x = snapToPhysical(@as(f32, @floatFromInt(tx)) + (@as(f32, @floatFromInt(tw)) - cs.x) * 0.5, dpi_scale.x),
            .y = snapToPhysical(@as(f32, @floatFromInt(ty)) + (@as(f32, @floatFromInt(th)) - cs.y) * 0.5, dpi_scale.y),
        }, icon_font, fg);
        return;
    }
    var tx: c_int = 0;
    var ty: c_int = 0;
    var tw: c_int = 0;
    var th: c_int = 0;
    splitterToggleBounds(strip_w, scr_h, &tx, &ty, &tw, &th);
    const lt: [*c]const u8 = "<";
    const ls = measureText(font, lt, icon_font);
    drawText(font, lt, .{
        .x = snapToPhysical(@as(f32, @floatFromInt(tx)) + (@as(f32, @floatFromInt(tw)) - ls.x) * 0.5, dpi_scale.x),
        .y = snapToPhysical(@as(f32, @floatFromInt(ty)) + (@as(f32, @floatFromInt(th)) - ls.y) * 0.5, dpi_scale.y),
    }, icon_font, fg);
}
