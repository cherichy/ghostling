const std = @import("std");
const tab_runtime = @import("tab_runtime.zig");

const Tab = tab_runtime.Tab;

pub const ToggleW: i32 = 22;
pub const ToggleH: i32 = 28;
pub const IndexColW: i32 = 28;
pub const CloseW: i32 = 28;
pub const NewH: i32 = 44;
pub const CollapsedEdgeHover: i32 = 20;
pub const SplitterGrab: i32 = 8;
pub const EditNone: usize = std.math.maxInt(usize);

pub fn snapToPhysical(value: f32, scale: f32) f32 {
    const s = if (scale > 0.0) scale else 1.0;
    const scaled = value * s;
    const snapped = if (scaled >= 0.0) @floor(scaled + 0.5) else @ceil(scaled - 0.5);
    return snapped / s;
}

pub fn quantizeFontSize(font_size: f32, dpi_y: f32) f32 {
    return snapToPhysical(font_size, dpi_y);
}
