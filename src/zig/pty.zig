const std = @import("std");
const builtin = @import("builtin");

pub const PtyHandle = if (builtin.os.tag == .windows) ?*anyopaque else i32;
