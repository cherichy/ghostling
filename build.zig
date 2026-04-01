const std = @import("std");
const builtin = @import("builtin");

/// MSYS2 UCRT64 ships both `libraylib.a` (static) and `libraylib.dll.a` (import lib for `raylib.dll`).
/// `linkSystemLibrary("raylib")` / dropping `libraylib.a` on the link line often still resolves to the
/// **static** archive first. Link the `.dll.a` stubs explicitly so the exe depends on `raylib.dll` + `glfw3.dll`.
fn linkRaylibGlfwMingwDllImports(b: *std.Build, compile: *std.Build.Step.Compile, lib_dir: []const u8) void {
    compile.addObjectFile(.{ .cwd_relative = b.pathJoin(&.{ lib_dir, "libraylib.dll.a" }) });
    compile.addObjectFile(.{ .cwd_relative = b.pathJoin(&.{ lib_dir, "libglfw3.dll.a" }) });
}

pub fn build(b: *std.Build) void {
    const default_target: std.Target.Query = if (builtin.target.os.tag == .windows) blk: {
        break :blk std.Build.parseTargetQuery(.{ .arch_os_abi = "native-windows-gnu" }) catch .{};
    } else .{};

    const target = b.standardTargetOptions(.{ .default_target = default_target });
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
    const raylib_lib = b.option([]const u8, "raylib-lib", "Path to libraylib.a (directory must also contain libraylib.dll.a for MinGW dynamic link)") orelse "";
    const raylib_include = b.option([]const u8, "raylib-include", "Directory containing raylib.h (CMake: build/_deps/raylib-src/src)") orelse "";

    if (raylib_lib.len > 0 and raylib_include.len == 0 and raylib_prefix.len == 0) {
        std.debug.panic("-Draylib-lib requires -Draylib-include or -Draylib-prefix (for headers)\n", .{});
    }

    const ghostty_dep = b.dependency("ghostty", .{
        .target = target,
        .optimize = optimize,
        .@"emit-lib-vt" = true,
    });
    const ghostty_vt = ghostty_dep.artifact("ghostty-vt");

    const bin2header_tool = b.addExecutable(.{
        .name = "bin2header",
        .root_module = b.createModule(.{
            .root_source_file = b.path("tools/bin2header.zig"),
            .target = b.graph.host,
            .optimize = .Debug,
        }),
    });
    const gen_font_h = b.addRunArtifact(bin2header_tool);
    gen_font_h.addFileArg(b.path("fonts/JetBrainsMono-Regular.ttf"));
    const font_jbh = gen_font_h.addOutputFileArg("font_jetbrains_mono.h");
    gen_font_h.addArg("font_jetbrains_mono");

    const c_mod = b.createModule(.{
        .root_source_file = null,
        .target = target,
        .optimize = optimize,
    });
    c_mod.addIncludePath(ghostty_dep.path("include"));
    c_mod.addIncludePath(b.path("src/c"));
    c_mod.addIncludePath(font_jbh.dirname());

    const c_files: []const []const u8 = if (target.result.os.tag == .windows) &.{
        "src/c/main.c",
        "src/c/pty_common.c",
        "src/c/pty_win.c",
        "src/c/config_font.c",
        "src/c/osc52_clipboard.c",
        "src/c/effects.c",
        "src/c/tabs.c",
        "src/c/terminal_ui.c",
    } else &.{
        "src/c/main.c",
        "src/c/pty_common.c",
        "src/c/pty_unix.c",
        "src/c/config_font.c",
        "src/c/osc52_clipboard.c",
        "src/c/effects.c",
        "src/c/tabs.c",
        "src/c/terminal_ui.c",
    };
    const c_flags: []const []const u8 = if (target.result.os.tag == .linux)
        &.{ "-std=c11", "-D_DEFAULT_SOURCE" }
    else
        &.{"-std=c11"};
    c_mod.addCSourceFiles(.{
        .root = b.path(""),
        .files = c_files,
        .flags = c_flags,
    });

    const ghostling = b.addExecutable(.{
        .name = "ghostling",
        .root_module = c_mod,
    });
    ghostling.step.dependOn(&gen_font_h.step);
    ghostling.linkLibC();
    ghostling.linkLibCpp();
    ghostling.linkLibrary(ghostty_vt);

    const win_gnu = target.result.os.tag == .windows and target.result.abi == .gnu;
    const raylib_loc = raylib_prefix.len > 0 or raylib_lib.len > 0;

    if (win_gnu and raylib_loc) {
        const lib_dir: []const u8 = if (raylib_lib.len > 0)
            std.fs.path.dirname(raylib_lib) orelse "."
        else
            b.pathJoin(&.{ raylib_prefix, "lib" });
        if (raylib_include.len > 0) {
            c_mod.addIncludePath(.{ .cwd_relative = b.dupePath(raylib_include) });
        } else if (raylib_prefix.len > 0) {
            c_mod.addIncludePath(.{ .cwd_relative = b.pathJoin(&.{ raylib_prefix, "include" }) });
        }
        linkRaylibGlfwMingwDllImports(b, ghostling, lib_dir);
    } else if (raylib_lib.len > 0) {
        const dir = std.fs.path.dirname(raylib_lib) orelse ".";
        ghostling.addLibraryPath(.{ .cwd_relative = b.dupePath(dir) });
        if (raylib_include.len > 0) {
            c_mod.addIncludePath(.{ .cwd_relative = b.dupePath(raylib_include) });
        } else if (raylib_prefix.len > 0) {
            c_mod.addIncludePath(.{ .cwd_relative = b.pathJoin(&.{ raylib_prefix, "include" }) });
        }
        c_mod.linkSystemLibrary("raylib", .{
            .preferred_link_mode = .dynamic,
            .search_strategy = .mode_first,
        });
    } else {
        if (raylib_prefix.len > 0) {
            ghostling.addLibraryPath(.{ .cwd_relative = b.pathJoin(&.{ raylib_prefix, "lib" }) });
            c_mod.addIncludePath(.{ .cwd_relative = b.pathJoin(&.{ raylib_prefix, "include" }) });
        }
        c_mod.linkSystemLibrary("raylib", .{
            .preferred_link_mode = .dynamic,
            .search_strategy = .mode_first,
        });
    }

    switch (target.result.os.tag) {
        .windows => {
            ghostling.subsystem = .Windows;
            ghostling.linkSystemLibrary("opengl32");
            ghostling.linkSystemLibrary("gdi32");
            ghostling.linkSystemLibrary("winmm");
            if (!(win_gnu and raylib_loc)) c_mod.linkSystemLibrary("glfw3", .{
                .preferred_link_mode = .dynamic,
                .search_strategy = .mode_first,
            });
        },
        .macos => {
            ghostling.linkFramework("IOKit");
            ghostling.linkFramework("Cocoa");
            ghostling.linkFramework("OpenGL");
            c_mod.addRPathSpecial("@loader_path/../lib");
        },
        .linux => {
            ghostling.linkSystemLibrary("util");
            c_mod.addRPathSpecial("$ORIGIN/../lib");
        },
        else => {},
    }

    b.installArtifact(ghostling);
    b.installArtifact(ghostty_vt);

    b.step("ghostling-c", "Alias for default install (C sources under src/c/)").dependOn(b.getInstallStep());

    const run_step = b.step("run", "Run ghostling");
    const run_cmd = b.addRunArtifact(ghostling);
    run_cmd.step.dependOn(b.getInstallStep());
    if (b.args) |args| run_cmd.addArgs(args);
    run_step.dependOn(&run_cmd.step);
}
