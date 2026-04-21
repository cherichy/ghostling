const std = @import("std");

const rl = @cImport({
    @cInclude("raylib.h");
    @cInclude("tabs.h");
    @cInclude("agent_state.h");
    @cInclude("stdio.h");
});

pub const ToggleW: c_int = 22;
pub const ToggleH: c_int = 28;

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

fn truncateToWidth(font: rl.Font, font_size: f32, src: [*c]const u8, max_w: f32, out: [*c]u8, outsz: usize) void {
    if (max_w < 8.0) {
        if (outsz > 0) out[0] = 0;
        return;
    }
    if (rl.MeasureTextEx(font, src, font_size, 0).x <= max_w) {
        _ = rl.snprintf(out, outsz, "%s", src);
        return;
    }
    var tmp: [512]u8 = undefined;
    _ = rl.snprintf(&tmp, tmp.len, "%s", src);
    var n = std.mem.indexOfScalar(u8, &tmp, 0) orelse tmp.len;
    while (n > 0) {
        n -= 1;
        tmp[n] = 0;
        if (rl.MeasureTextEx(font, &tmp, font_size, 0).x <= max_w) {
            _ = rl.snprintf(out, outsz, "%s", &tmp);
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

export fn tab_splitter_hit(mpos: rl.Vector2, strip_w: c_int, scr_h: c_int) bool {
    _ = scr_h;
    return mpos.x >= @as(f32, @floatFromInt(strip_w)) and
        mpos.x <= @as(f32, @floatFromInt(strip_w + 8));
}

export fn tab_splitter_toggle_hit(mpos: rl.Vector2, effective_strip_w: c_int, scr_h: c_int) bool {
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

export fn tab_strip_hit(
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

    const new_y0 = scr_h - 44;
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
    if (mpos.x >= @as(f32, @floatFromInt(strip_w - 28)) and in_title) {
        act.* = 1;
    } else {
        act.* = 0;
    }
    return true;
}

export fn tab_strip_draw(
    font: rl.Font,
    font_size: f32,
    strip_w: c_int,
    scr_h: c_int,
    tabs: [*c]?*rl.Tab,
    n_tabs: usize,
    active_idx: usize,
    edit_idx: usize,
    edit_buf: [*c]const u8,
    strip_bg: rl.Color,
    tab_index_bg: rl.Color,
    tab_reserved_bg: rl.Color,
    tab_bg: rl.Color,
    tab_active: rl.Color,
    border: rl.Color,
    fg: rl.Color,
    edit_bg: rl.Color,
    tab_title_h: c_int,
    tab_reserved_h: c_int,
    strip_collapsed: bool,
) void {
    _ = edit_buf;
    if (strip_collapsed) return;

    const dpi_scale = currentDpiScale();
    const qfont = quantizeFontSize(font_size, dpi_scale.y);

    const title_h = if (tab_title_h < 1) 1 else tab_title_h;
    const reserved_h = if (tab_reserved_h < 0) 0 else tab_reserved_h;
    const row_h = title_h + reserved_h;
    if (row_h < 1) return;

    const ix: c_int = 28;
    const close_w: c_int = 28;

    rl.DrawRectangle(0, 0, strip_w, scr_h, strip_bg);
    rl.DrawRectangle(strip_w - 1, 0, 1, scr_h, border);

    const new_y0 = scr_h - 44;
    const max_vis = if (new_y0 > 0) @as(usize, @intCast(@divTrunc(new_y0, row_h))) else 1;

    var i: usize = 0;
    while (i < n_tabs and i < max_vis) : (i += 1) {
        const y0 = @as(c_int, @intCast(i * @as(usize, @intCast(row_h))));
        const editing = (edit_idx == i);
        const tab_ptr = tabs[i] orelse continue;

        rl.DrawRectangle(0, y0, ix, row_h, tab_index_bg);
        rl.DrawRectangle(ix - 1, y0, 1, row_h, border);

        const title_bg = if (editing) edit_bg else if (i == active_idx) tab_active else tab_bg;
        rl.DrawRectangle(ix, y0, strip_w - ix - 1, title_h, title_bg);

        const y_res = y0 + title_h;
        rl.DrawRectangle(ix, y_res, strip_w - ix - 1, reserved_h, tab_reserved_bg);
        rl.DrawRectangle(ix, y0 + title_h - 1, strip_w - ix - 1, 1, border);
        rl.DrawRectangle(0, y_res + reserved_h - 1, strip_w - 1, 1, border);

        var num_buf: [8]u8 = undefined;
        const num_slice = std.fmt.bufPrint(&num_buf, "{}", .{i + 1}) catch "?";
        const ns = rl.MeasureTextEx(font, @as([*c]const u8, @ptrCast(num_slice.ptr)), qfont, 0);
        var nx = (@as(f32, @floatFromInt(ix)) - ns.x) * 0.5;
        if (nx < 2.0) nx = 2.0;
        const ny = @as(f32, @floatFromInt(y0)) + (@as(f32, @floatFromInt(row_h)) - ns.y) * 0.5;
        const num_pos = rl.Vector2{ .x = snapToPhysical(nx, dpi_scale.x), .y = snapToPhysical(ny, dpi_scale.y) };
        if (i == active_idx) {
            drawTextSyntheticBold(font, @as([*c]const u8, @ptrCast(num_slice.ptr)), num_pos, qfont, fg, dpi_scale);
        } else {
            rl.DrawTextEx(font, @as([*c]const u8, @ptrCast(num_slice.ptr)), num_pos, qfont, 0, fg);
        }

        var title_buf: [256]u8 = undefined;
        rl.tab_display_title(tab_ptr, i + 1, &title_buf, @as(c_int, title_buf.len));

        const title_slice = std.mem.sliceTo(&title_buf, 0);
        const title_text = if (title_slice.len > 0) title_slice else "?";
        const ts = rl.MeasureTextEx(font, @as([*c]const u8, @ptrCast(title_text.ptr)), qfont, 0);
        const tx = @as(f32, @floatFromInt(ix)) + 6.0;
        const ty = @as(f32, @floatFromInt(y0)) + (@as(f32, @floatFromInt(title_h)) - ts.y) * 0.5;
        rl.DrawTextEx(font, @as([*c]const u8, @ptrCast(title_text.ptr)), .{ .x = snapToPhysical(tx, dpi_scale.x), .y = snapToPhysical(ty, dpi_scale.y) }, qfont, 0, fg);

        if (reserved_h > 0) {
            const agent_label = rl.ghostling_agent_state_label(&tab_ptr.agent_state);
            const als = rl.MeasureTextEx(font, agent_label, qfont, 0);
            const ay = @as(f32, @floatFromInt(y_res)) + (@as(f32, @floatFromInt(reserved_h)) - als.y) * 0.5;
            rl.DrawTextEx(font, agent_label, .{ .x = snapToPhysical(tx, dpi_scale.x), .y = snapToPhysical(ay, dpi_scale.y) }, qfont, 0, fg);
        }

        const x_str = "×";
        const xs = rl.MeasureTextEx(font, x_str, qfont, 0);
        const cx = @as(f32, @floatFromInt(strip_w - close_w)) + ((@as(f32, @floatFromInt(close_w)) - xs.x) * 0.5);
        rl.DrawTextEx(font, x_str, .{ .x = snapToPhysical(cx, dpi_scale.x), .y = snapToPhysical(ty, dpi_scale.y) }, qfont, 0, fg);
    }

    rl.DrawRectangle(0, new_y0, strip_w - 1, 44, tab_bg);
    rl.DrawRectangle(0, new_y0, strip_w - 1, 1, border);
    const plus = "+";
    const icon_font = quantizeFontSize(iconFontSize(qfont), dpi_scale.y);
    const ps = rl.MeasureTextEx(font, plus, icon_font, 0);
    drawTextSyntheticBold(font, plus, .{
        .x = snapToPhysical((@as(f32, @floatFromInt(strip_w)) - ps.x) * 0.5, dpi_scale.x),
        .y = snapToPhysical(@as(f32, @floatFromInt(new_y0)) + (@as(f32, @floatFromInt(44)) - ps.y) * 0.5, dpi_scale.y),
    }, icon_font, fg, dpi_scale);
}

export fn tab_splitter_toggle_draw(
    font: rl.Font,
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
        const ch = ">";
        const cs = rl.MeasureTextEx(font, ch, icon_font, 0);
        rl.DrawTextEx(font, ch, .{
            .x = snapToPhysical(@as(f32, @floatFromInt(tx)) + (@as(f32, @floatFromInt(tw)) - cs.x) * 0.5, dpi_scale.x),
            .y = snapToPhysical(@as(f32, @floatFromInt(ty)) + (@as(f32, @floatFromInt(th)) - cs.y) * 0.5, dpi_scale.y),
        }, icon_font, 0, fg);
        return;
    }
    var tx: c_int = 0;
    var ty: c_int = 0;
    var tw: c_int = 0;
    var th: c_int = 0;
    splitterToggleBounds(strip_w, scr_h, &tx, &ty, &tw, &th);
    const lt = "<";
    const ls = rl.MeasureTextEx(font, lt, icon_font, 0);
    rl.DrawTextEx(font, lt, .{
        .x = snapToPhysical(@as(f32, @floatFromInt(tx)) + (@as(f32, @floatFromInt(tw)) - ls.x) * 0.5, dpi_scale.x),
        .y = snapToPhysical(@as(f32, @floatFromInt(ty)) + (@as(f32, @floatFromInt(th)) - ls.y) * 0.5, dpi_scale.y),
    }, icon_font, 0, fg);
}
