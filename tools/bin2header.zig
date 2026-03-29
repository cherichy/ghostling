//! Emit a C header with `static const unsigned char NAME[] = { ... };`
//! from a binary file. Args: <input_path> <output_path> <array_name>
const std = @import("std");

pub fn main() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{}){};
    defer _ = gpa.deinit();
    const alloc = gpa.allocator();

    const args = try std.process.argsAlloc(alloc);
    defer std.process.argsFree(alloc, args);
    if (args.len != 4) {
        std.debug.print("usage: bin2header <input> <output> <array_name>\n", .{});
        std.process.exit(1);
    }

    const input_path = args[1];
    const output_path = args[2];
    const array_name = args[3];

    const data = try std.fs.cwd().readFileAlloc(alloc, input_path, std.math.maxInt(usize));
    defer alloc.free(data);

    var list = std.array_list.Managed(u8).init(alloc);
    defer list.deinit();

    try list.writer().print("// Auto-generated from {s} — do not edit.\n", .{input_path});
    try list.writer().print("static const unsigned char {s}[] = {{\n    ", .{array_name});

    for (data, 0..) |byte, i| {
        if (i > 0) {
            try list.writer().print(", ", .{});
            if (i % 12 == 0) try list.writer().print("\n    ", .{});
        }
        try list.writer().print("0x{X:0>2}", .{byte});
    }
    try list.writer().print("\n}};\n", .{});

    try std.fs.cwd().writeFile(.{ .sub_path = output_path, .data = list.items });
}
