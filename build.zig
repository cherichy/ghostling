const std = @import("std");
const builtin = @import("builtin");

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
        if (b.option(std.builtin.OptimizeMode, "optimize", "Optimization mode")) |m| break :blk m;
        break :blk switch (b.release_mode) {
            .off => .ReleaseFast,
            .any => .ReleaseFast,
            .fast => .ReleaseFast,
            .safe => .ReleaseSafe,
            .small => .ReleaseSmall,
        };
    };

    const raylib_prefix = b.option([]const u8, "raylib-prefix", "Install root with include/ and lib/") orelse "";
    const raylib_lib = b.option([]const u8, "raylib-lib", "Path to libraylib.a") orelse "";
    const raylib_include = b.option([]const u8, "raylib-include", "Directory containing raylib.h") orelse "";

    if (raylib_lib.len > 0 and raylib_include.len == 0 and raylib_prefix.len == 0) {
        std.debug.panic("-Draylib-lib requires -Draylib-include or -Draylib-prefix\n", .{});
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
        .root_source_file = b.path("src/zig/main.zig"),
        .target = target,
        .optimize = optimize,
    });
    c_mod.addIncludePath(ghostty_dep.path("include"));
    c_mod.addIncludePath(b.path("src/c"));
    c_mod.addIncludePath(font_jbh.dirname());
    if (target.result.os.tag == .macos) {
        c_mod.addIncludePath(.{ .cwd_relative = "/opt/homebrew/include" });
    }

    const c_files: []const []const u8 = if (target.result.os.tag == .windows) &.{
        "src/c/pty_win.c",
        "src/c/terminal_ui.c",
        "src/c/tab_ui.c",
    } else &.{
        "src/c/terminal_ui.c",
        "src/c/tab_ui.c",
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

    const osc52_mod = b.createModule(.{
        .root_source_file = b.path("src/zig/osc52_clipboard.zig"),
        .target = target,
        .optimize = optimize,
    });
    osc52_mod.addIncludePath(ghostty_dep.path("include"));
    const osc52_obj = b.addObject(.{
        .name = "osc52_clipboard",
        .root_module = osc52_mod,
    });

    const agent_events_mod = b.createModule(.{
        .root_source_file = b.path("src/zig/agent_events.zig"),
        .target = target,
        .optimize = optimize,
    });
    agent_events_mod.addIncludePath(ghostty_dep.path("include"));
    agent_events_mod.addIncludePath(b.path("src/c"));
    const agent_events_obj = b.addObject(.{
        .name = "agent_events",
        .root_module = agent_events_mod,
    });

    const agent_state_mod = b.createModule(.{
        .root_source_file = b.path("src/zig/agent_state.zig"),
        .target = target,
        .optimize = optimize,
    });
    agent_state_mod.addIncludePath(ghostty_dep.path("include"));
    agent_state_mod.addIncludePath(b.path("src/c"));
    const agent_state_obj = b.addObject(.{
        .name = "agent_state",
        .root_module = agent_state_mod,
    });

    const effects_mod = b.createModule(.{
        .root_source_file = b.path("src/zig/effects.zig"),
        .target = target,
        .optimize = optimize,
    });
    effects_mod.addIncludePath(ghostty_dep.path("include"));
    effects_mod.addIncludePath(b.path("src/c"));
    const effects_obj = b.addObject(.{
        .name = "effects",
        .root_module = effects_mod,
    });

    const config_font_mod = b.createModule(.{
        .root_source_file = b.path("src/zig/config_font.zig"),
        .target = target,
        .optimize = optimize,
    });
    config_font_mod.addIncludePath(ghostty_dep.path("include"));
    config_font_mod.addIncludePath(b.path("src/c"));
    if (target.result.os.tag == .macos) {
        config_font_mod.addIncludePath(.{ .cwd_relative = "/opt/homebrew/include" });
    }
    const config_font_obj = b.addObject(.{
        .name = "config_font",
        .root_module = config_font_mod,
    });

    const tab_runtime_mod = b.createModule(.{
        .root_source_file = b.path("src/zig/tab_runtime.zig"),
        .target = target,
        .optimize = optimize,
    });
    tab_runtime_mod.addIncludePath(ghostty_dep.path("include"));
    tab_runtime_mod.addIncludePath(b.path("src/c"));
    if (target.result.os.tag == .macos) {
        tab_runtime_mod.addIncludePath(.{ .cwd_relative = "/opt/homebrew/include" });
    }
    const tab_runtime_obj = b.addObject(.{
        .name = "tab_runtime",
        .root_module = tab_runtime_mod,
    });

    const pty_mod = b.createModule(.{
        .root_source_file = b.path("src/zig/pty.zig"),
        .target = target,
        .optimize = optimize,
    });
    pty_mod.addIncludePath(b.path("src/c"));
    const pty_obj = b.addObject(.{
        .name = "pty",
        .root_module = pty_mod,
    });

    const ghostling = b.addExecutable(.{
        .name = "ghostling",
        .root_module = c_mod,
    });
    ghostling.step.dependOn(&gen_font_h.step);
    ghostling.linkLibC();
    ghostling.linkLibCpp();
    ghostling.linkLibrary(ghostty_vt);
    ghostling.addObject(osc52_obj);
    ghostling.addObject(agent_events_obj);
    ghostling.addObject(agent_state_obj);
    ghostling.addObject(effects_obj);
    ghostling.addObject(config_font_obj);
    ghostling.addObject(tab_runtime_obj);
    ghostling.addObject(pty_obj);
    const win_gnu = target.result.os.tag == .windows and target.result.abi == .gnu;
    const raylib_loc = raylib_prefix.len > 0 or raylib_lib.len > 0;

    if (win_gnu and raylib_loc) {
        const lib_dir: []const u8 = if (raylib_lib.len > 0)
            std.fs.path.dirname(raylib_lib) orelse "."
        else
            b.pathJoin(&.{ raylib_prefix, "lib" });
        if (raylib_include.len > 0) {
            c_mod.addIncludePath(.{ .cwd_relative = b.dupePath(raylib_include) });
            osc52_mod.addIncludePath(.{ .cwd_relative = b.dupePath(raylib_include) });
        } else if (raylib_prefix.len > 0) {
            const rl_inc = b.pathJoin(&.{ raylib_prefix, "include" });
            c_mod.addIncludePath(.{ .cwd_relative = rl_inc });
            osc52_mod.addIncludePath(.{ .cwd_relative = rl_inc });
        }
        linkRaylibGlfwMingwDllImports(b, ghostling, lib_dir);
    } else if (raylib_lib.len > 0) {
        const dir = std.fs.path.dirname(raylib_lib) orelse ".";
        ghostling.addLibraryPath(.{ .cwd_relative = b.dupePath(dir) });
        if (raylib_include.len > 0) {
            c_mod.addIncludePath(.{ .cwd_relative = b.dupePath(raylib_include) });
            osc52_mod.addIncludePath(.{ .cwd_relative = b.dupePath(raylib_include) });
        } else if (raylib_prefix.len > 0) {
            const rl_inc = b.pathJoin(&.{ raylib_prefix, "include" });
            c_mod.addIncludePath(.{ .cwd_relative = rl_inc });
            osc52_mod.addIncludePath(.{ .cwd_relative = rl_inc });
        }
        c_mod.linkSystemLibrary("raylib", .{
            .preferred_link_mode = .dynamic,
            .search_strategy = .mode_first,
        });
    } else {
        if (raylib_prefix.len > 0) {
            const rl_inc = b.pathJoin(&.{ raylib_prefix, "include" });
            ghostling.addLibraryPath(.{ .cwd_relative = b.pathJoin(&.{ raylib_prefix, "lib" }) });
            c_mod.addIncludePath(.{ .cwd_relative = rl_inc });
            osc52_mod.addIncludePath(.{ .cwd_relative = rl_inc });
        } else if (target.result.os.tag == .macos) {
            osc52_mod.addIncludePath(.{ .cwd_relative = "/opt/homebrew/include" });
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
