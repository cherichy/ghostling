const std = @import("std");

const c = @cImport({
    @cInclude("effects.h");
    @cInclude("pty_common.h");
});

export fn effect_write_pty(
    terminal: c.GhosttyTerminal,
    userdata: ?*anyopaque,
    data: [*c]const u8,
    len: usize,
) void {
    _ = terminal;
    const ctx = @as(*c.EffectsContext, @ptrCast(@alignCast(userdata)));
    _ = ctx;
    _ = data;
    _ = len;
}

export fn effect_size(
    terminal: c.GhosttyTerminal,
    userdata: ?*anyopaque,
    out_size: ?*anyopaque,
) bool {
    _ = terminal;
    const ctx = @as(*c.EffectsContext, @ptrCast(@alignCast(userdata)));
    const out = @as(*c.GhosttySizeReportSize, @ptrCast(@alignCast(out_size)));
    out.* = .{
        .rows = ctx.rows,
        .columns = ctx.cols,
        .cell_width = @as(u32, @intCast(ctx.cell_width)),
        .cell_height = @as(u32, @intCast(ctx.cell_height)),
    };
    return true;
}

export fn effect_device_attributes(
    terminal: c.GhosttyTerminal,
    userdata: ?*anyopaque,
    out_attrs: ?*anyopaque,
) bool {
    _ = terminal;
    _ = userdata;
    const out = @as(*c.GhosttyDeviceAttributes, @ptrCast(@alignCast(out_attrs)));
    @memset(@as([*]u8, @ptrCast(out))[0..@sizeOf(c.GhosttyDeviceAttributes)], 0);
    out.primary.conformance_level = c.GHOSTTY_DA_CONFORMANCE_VT220;
    out.primary.features[0] = c.GHOSTTY_DA_FEATURE_COLUMNS_132;
    out.primary.features[1] = c.GHOSTTY_DA_FEATURE_SELECTIVE_ERASE;
    out.primary.features[2] = c.GHOSTTY_DA_FEATURE_ANSI_COLOR;
    out.primary.features[3] = c.GHOSTTY_DA_FEATURE_CLIPBOARD;
    out.primary.num_features = 4;
    out.secondary.device_type = c.GHOSTTY_DA_DEVICE_TYPE_VT220;
    out.secondary.firmware_version = 1;
    out.secondary.rom_cartridge = 0;
    out.tertiary.unit_id = 0;
    return true;
}

export fn effect_xtversion(
    terminal: c.GhosttyTerminal,
    userdata: ?*anyopaque,
) c.GhosttyString {
    _ = terminal;
    _ = userdata;
    return .{
        .ptr = @as([*c]const u8, @ptrCast("ghostling")),
        .len = 9,
    };
}

export fn effect_title_changed(
    terminal: c.GhosttyTerminal,
    userdata: ?*anyopaque,
) void {
    const ctx = @as(*c.EffectsContext, @ptrCast(@alignCast(userdata)));
    var title: c.GhosttyString = .{ .ptr = null, .len = 0 };
    if (c.ghostty_terminal_get(terminal, c.GHOSTTY_TERMINAL_DATA_TITLE, &title) != c.GHOSTTY_SUCCESS) return;
    const len = @min(title.len, ctx.title_shell.len - 1);
    @memcpy(ctx.title_shell[0..len], title.ptr[0..len]);
    ctx.title_shell[len] = 0;
}

export fn effect_sync_pwd(
    terminal: c.GhosttyTerminal,
    userdata: ?*anyopaque,
) void {
    const ctx = @as(*c.EffectsContext, @ptrCast(@alignCast(userdata)));
    var pwd: c.GhosttyString = .{ .ptr = null, .len = 0 };
    if (c.ghostty_terminal_get(terminal, c.GHOSTTY_TERMINAL_DATA_PWD, &pwd) != c.GHOSTTY_SUCCESS) return;
    const len = @min(pwd.len, ctx.pwd.len - 1);
    @memcpy(ctx.pwd[0..len], pwd.ptr[0..len]);
    ctx.pwd[len] = 0;
}

export fn effect_color_scheme(
    terminal: c.GhosttyTerminal,
    userdata: ?*anyopaque,
    out_scheme: ?*anyopaque,
) bool {
    _ = terminal;
    _ = userdata;
    _ = out_scheme;
    return false;
}

export fn effect_scan_icon_osc(
    ctx: *c.EffectsContext,
    data: [*c]const u8,
    len: usize,
) void {
    const slice = data[0..len];
    for (slice) |b| {
        switch (ctx.icon_osc_mode) {
            0 => {
                if (b == 0x1B) ctx.icon_osc_mode = 1;
            },
            1 => {
                if (b == ']') {
                    ctx.icon_osc_mode = 2;
                    iconOscResetCommand(ctx);
                } else if (b == 0x1B) {
                    ctx.icon_osc_mode = 1;
                } else {
                    ctx.icon_osc_mode = 0;
                }
            },
            2 => {
                if (ctx.icon_osc_esc_pending) {
                    ctx.icon_osc_esc_pending = false;
                    if (b == '\\') {
                        iconOscFinish(ctx);
                    } else {
                        iconOscProcessByte(ctx, 0x1B);
                        iconOscProcessByte(ctx, b);
                    }
                } else if (b == 0x07) {
                    iconOscFinish(ctx);
                } else if (b == 0x1B) {
                    ctx.icon_osc_esc_pending = true;
                } else {
                    iconOscProcessByte(ctx, b);
                }
            },
            else => ctx.icon_osc_mode = 0,
        }
    }
}

fn iconOscResetCommand(ctx: *c.EffectsContext) void {
    ctx.icon_osc_esc_pending = false;
    ctx.icon_osc_cmd_decided = false;
    ctx.icon_osc_collect = true;
    ctx.icon_osc_cmd = 0;
    ctx.icon_osc_cmd_digits = 0;
    ctx.icon_osc_payload_len = 0;
    ctx.icon_osc_payload[0] = 0;
}

fn iconOscProcessByte(ctx: *c.EffectsContext, b: u8) void {
    if (!ctx.icon_osc_cmd_decided) {
        if (b >= '0' and b <= '9') {
            if (ctx.icon_osc_cmd <= 99999999) ctx.icon_osc_cmd = ctx.icon_osc_cmd * 10 + (b - '0');
            ctx.icon_osc_cmd_digits += 1;
            return;
        }
        if (b == ';') {
            ctx.icon_osc_cmd_decided = true;
            ctx.icon_osc_collect = ctx.icon_osc_cmd_digits > 0 and (ctx.icon_osc_cmd == 1 or ctx.icon_osc_cmd == 2);
            return;
        }
        ctx.icon_osc_cmd_decided = true;
        ctx.icon_osc_collect = false;
        return;
    }
    if (!ctx.icon_osc_collect) return;
    if (ctx.icon_osc_payload_len + 1 >= ctx.icon_osc_payload.len) {
        ctx.icon_osc_collect = false;
        ctx.icon_osc_payload_len = 0;
        ctx.icon_osc_payload[0] = 0;
        return;
    }
    ctx.icon_osc_payload[ctx.icon_osc_payload_len] = b;
    ctx.icon_osc_payload_len += 1;
    ctx.icon_osc_payload[ctx.icon_osc_payload_len] = 0;
}

fn iconOscFinish(ctx: *c.EffectsContext) void {
    if (ctx.icon_osc_collect and ctx.icon_osc_payload_len > 0) {
        const len = @min(ctx.icon_osc_payload_len, ctx.title_icon.len - 1);
        @memcpy(ctx.title_icon[0..len], ctx.icon_osc_payload[0..len]);
        ctx.title_icon[len] = 0;
    }
    ctx.icon_osc_mode = 0;
    iconOscResetCommand(ctx);
}
