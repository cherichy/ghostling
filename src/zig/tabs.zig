const std = @import("std");

const c = @cImport({
    @cInclude("tabs.h");
    @cInclude("pty_common.h");
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
