const std = @import("std");
const builtin = @import("builtin");

pub fn build(b: *std.Build) void {
    // On Windows hosts, default to native-windows-gnu so Ghostty's C/C++ deps use
    // Zig's bundled MinGW libc (MSYS2 raylib matches GNU ABI). Override with
    // `-Dtarget=x86_64-windows-msvc` if you use Zig's MSVC libc integration.
    const default_target: std.Target.Query = if (builtin.target.os.tag == .windows) blk: {
        break :blk std.Build.parseTargetQuery(.{ .arch_os_abi = "native-windows-gnu" }) catch .{};
    } else .{};

    const target = b.standardTargetOptions(.{ .default_target = default_target });
    // libghostty-vt in Debug is extremely slow; default to ReleaseFast. Use
    // `-Doptimize=Debug` when you need a debuggable build.
    const optimize: std.builtin.OptimizeMode = blk: {
        if (b.option(
            std.builtin.OptimizeMode,
            "optimize",
            "Build mode (default: ReleaseFast; Debug is very slow for Ghostty)",
        )) |m| break :blk m;
        break :blk switch (b.release_mode) {
            .off => .ReleaseFast,
            .any => .ReleaseFast,
            .fast => .ReleaseFast,
            .safe => .ReleaseSafe,
            .small => .ReleaseSmall,
        };
    };

    const raylib_prefix = b.option([]const u8, "raylib-prefix", "Install root with include/ and lib/ (e.g. MSYS2 UCRT64)") orelse "";
    const raylib_lib = b.option([]const u8, "raylib-lib", "Path to libraylib.a (CMake: build/_deps/raylib-build/raylib/libraylib.a)") orelse "";
    const raylib_include = b.option([]const u8, "raylib-include", "Directory containing raylib.h (CMake: build/_deps/raylib-src/src)") orelse "";

    if (raylib_lib.len > 0 and raylib_include.len == 0 and raylib_prefix.len == 0) {
        std.debug.panic("-Draylib-lib requires -Draylib-include or -Draylib-prefix (for headers)\n", .{});
    }

    const ghostty_dep = b.dependency("ghostty", .{
        .target = target,
        .optimize = optimize,
        .@"emit-lib-vt" = true,
    });
    // Shared libghostty-vt (DLL / .so / .dylib) — faster links when you rarely
    // rebuild Ghostty. CMake already uses the `ghostty-vt` imported target.
    const ghostty_vt = ghostty_dep.artifact("ghostty-vt");

    const bin2header = b.addExecutable(.{
        .name = "bin2header",
        .root_module = b.createModule(.{
            .root_source_file = b.path("tools/bin2header.zig"),
            .target = b.graph.host,
            .optimize = optimize,
        }),
    });

    const gen_font = b.addRunArtifact(bin2header);
    gen_font.addFileArg(b.path("fonts/JetBrainsMono-Regular.ttf"));
    const font_header = gen_font.addOutputFileArg("font_jetbrains_mono.h");
    gen_font.addArg("font_jetbrains_mono");

    const exe = b.addExecutable(.{
        .name = "ghostling",
        .root_module = b.createModule(.{
            .root_source_file = null,
            .target = target,
            .optimize = optimize,
        }),
    });
    exe.linkLibC();
    exe.root_module.addIncludePath(font_header.dirname());
    exe.root_module.addIncludePath(ghostty_dep.path("include"));
    exe.addCSourceFile(.{ .file = b.path("main.c"), .flags = &.{
        "-std=c11",
        "-Wall",
        "-Wextra",
    } });
    exe.step.dependOn(&gen_font.step);

    exe.linkLibrary(ghostty_vt);

    if (raylib_lib.len > 0) {
        exe.addObjectFile(.{ .cwd_relative = b.dupePath(raylib_lib) });
        if (raylib_include.len > 0) {
            exe.root_module.addIncludePath(.{ .cwd_relative = b.dupePath(raylib_include) });
        } else if (raylib_prefix.len > 0) {
            exe.root_module.addIncludePath(.{ .cwd_relative = b.pathJoin(&.{ raylib_prefix, "include" }) });
        }
    } else {
        if (raylib_prefix.len > 0) {
            exe.addLibraryPath(.{ .cwd_relative = b.pathJoin(&.{ raylib_prefix, "lib" }) });
            exe.root_module.addIncludePath(.{ .cwd_relative = b.pathJoin(&.{ raylib_prefix, "include" }) });
        }
        exe.linkSystemLibrary("raylib");
    }

    switch (target.result.os.tag) {
        .windows => {
            exe.subsystem = .Windows;
            exe.linkSystemLibrary("opengl32");
            exe.linkSystemLibrary("gdi32");
            exe.linkSystemLibrary("winmm");
        },
        .macos => {
            exe.linkFramework("IOKit");
            exe.linkFramework("Cocoa");
            exe.linkFramework("OpenGL");
            exe.root_module.addRPathSpecial("@loader_path/../lib");
        },
        .linux => {
            exe.linkSystemLibrary("util");
            // Shared lib installs under prefix/lib/; exe is in bin/.
            exe.root_module.addRPathSpecial("$ORIGIN/../lib");
        },
        else => {},
    }

    b.installArtifact(exe);
    b.installArtifact(ghostty_vt);

    const run_step = b.step("run", "Run ghostling");
    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());
    if (b.args) |args| run_cmd.addArgs(args);
    run_step.dependOn(&run_cmd.step);
}
