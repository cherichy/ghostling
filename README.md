# Ghostling

Ghostling is a terminal emulator built on [libghostty-vt](https://ghostty.org),
using [Raylib](https://www.raylib.com/) for windowing and 2D rendering.
[C sources under `src/c/`](https://github.com/cherichy/ghostling/tree/feat/windows-port/src/c) (entry `main.c`).

It started as a minimal demo of the libghostty C API but has grown into a
lightweight, configurable terminal with multi-tab support, CJK font handling,
clipboard integration, and AI agent awareness.

<p align="center">
  <img src="demo.gif" alt="Ghostling Demo" />
</p>

## What is Libghostty?

Libghostty is an embeddable library extracted from [Ghostty's](https://ghostty.org) core,
exposing a C and Zig API so any application can embed correct, fast terminal
emulation.

Ghostling uses **libghostty-vt**, a zero-dependency library (not even libc) that
handles VT sequence parsing, terminal state management (cursor position,
styles, text reflow, scrollback, etc.), and renderer state management. It
contains no renderer drawing or windowing code; the consumer (Ghostling, in
this case) provides its own. The core logic is extracted directly from Ghostty
and inherits all of its real-world benefits: excellent, accurate, and complete
terminal emulation support, SIMD-optimized parsing, leading Unicode support,
highly optimized memory usage, and a robust, fuzzed, and tested codebase, all
proven by millions of daily active users of Ghostty GUI.

## Features

### Terminal Emulation (via libghostty-vt)

- Resize with text reflow
- Full 24-bit color and 256-color palette support
- Bold, italic, and inverse text styles
- Unicode and multi-codepoint grapheme handling
- Keyboard input with modifier support (Shift, Ctrl, Alt, Super)
- Kitty keyboard protocol support
- Mouse tracking (X10, normal, button, and any-event modes)
- Mouse reporting formats (SGR, URxvt, UTF8, X10)
- Focus reporting (CSI I / CSI O)
- And more — effectively all terminal emulation features supported by Ghostty

### Ghostling Features

- **Multi-tab** — create, close, rename, and switch tabs; collapsible tab strip
- **OSC 52 clipboard** — terminal programs can write to the system clipboard
- **OSC 8 hyperlinks** — clickable URLs with hover cursor
- **Selection & copy** — mouse drag selection with configurable copy-on-select
- **Bracketed paste** — auto-wraps clipboard content when the terminal requests it
- **Scrollbar** — draggable scrollbar with mouse wheel support
- **CJK tiered font loading** — three-tier Han character sets (3500/6500/8105) with automatic runtime upgrade when missing characters are encountered
- **AI agent state tracking** — detects agent status via OSC 99 protocol, heuristics, and process monitoring; displayed in tab status bar
- **Configurable key bindings** — tab management, copy/paste, and config reload shortcuts
- **Config profiles** — named profiles selectable via env var or config file
- **Hot-reload** — config changes are detected and applied automatically, no restart needed
- **Adaptive frame rate** — 60fps active, 8fps idle, 4fps when unfocused

### What Is Coming

These features aren't properly exposed by libghostty-vt yet but will be:

- Kitty Graphics Protocol

This list is incomplete and we'll add things as we find them.

### Limitations Due to Upstreams

- Kitty keyboard protocol support is broken with some inputs. This is
  due to limitations of the underlying Raylib input system; it doesn't
  support rich enough input events to fully and correctly implement the Kitty
  keyboard protocol. This is a [known issue](https://github.com/glfw/glfw/issues/1502).
  The libghostty-vt API supports Kitty keyboard protocol correctly, but
  requires correct input events to do so.

## Building

Requirements:

- [Zig](https://ziglang.org/) 0.15.2+ on PATH
- Raylib (headers + library; e.g. via system package manager or MSYS2)
- macOS: [Command Line Tools or Xcode](https://developer.apple.com/xcode/)
- Linux (Ubuntu/Debian): `sudo apt install -y ninja-build build-essential git libxinerama-dev libxcursor-dev libxrandr-dev libxi-dev libxext-dev libx11-dev libgl-dev`

### Zig Build (recommended)

```sh
zig build                    # Linux / macOS (raylib in system paths)
zig build run                # build and run
```

On **Windows** with MSYS2 raylib:

```sh
zig build -Draylib-prefix="C:/path/to/msys2/clang64"
.\zig-out\bin\ghostling.exe
```

See `zig build -h` for all options (`-Draylib-lib=...`, `-Draylib-include=...`, `-Dtarget=...`).

`zig build` defaults to **ReleaseFast** for libghostty-vt because Debug is very slow.
Use `-Doptimize=Debug` only when you need symbols.

> [!WARNING]
>
> Debug builds are VERY SLOW since Ghostty includes extra safety and
> correctness checks. Do not benchmark debug builds.

### CMake (legacy)

```sh
cmake -B build -G Ninja
cmake --build build
./build/ghostling          # Linux / macOS
.\build\ghostling.exe      # Windows
```

### Shell Selection (Windows)

On Windows, ghostling uses ConPTY and auto-detects the best available
shell (pwsh > powershell > cmd). Pass a shell path as the first argument
to override (e.g. `ghostling.exe C:\Windows\System32\cmd.exe`).

## Configuration

Ghostling reads a plain-text config file at startup. See
[CONFIG.md](CONFIG.md) for full documentation (Chinese), or copy
[config.example](config.example) to get started:

| Platform | Config path |
|----------|-------------|
| Windows  | `%APPDATA%\ghostling\config` |
| Linux / macOS | `~/.config/ghostling/config` |

Key settings: font path/size, codepoint set (CJK tiers), tab strip layout,
copy/paste shortcuts, tab management keybindings, profiles, and more.
Changes are hot-reloaded automatically.

## FAQ

### Why Not Zig?

libghostty-vt has a fully capable and proven Zig API. Ghostty GUI itself
uses this and is a good — although complex — example of how to use it.
However, this project uses the C API since C is so much more broadly used
and accessible to a wide variety of developers and language ecosystems.

### What about Rust or any other language?

libghostty-vt has a C API and can have zero dependencies, so it can be used
with minimally thin bindings in basically any language. I'm not sure yet if
the Ghostty project will maintain official bindings for languages other than C
and Zig, but I hope the community will create and maintain bindings for many
languages!

### Does libghostty require Raylib?

**No!** libghostty has no opinion about the renderer or GUI framework
used; it's even standalone WASM-compatible for browsers and other environments.

libghostty provides a [high-performance render state API](https://libghostty.tip.ghostty.org/group__render.html)
which only keeps track of the _state_ required to build a renderer. This is the
same API used by Ghostty GUI for Metal and OpenGL rendering and in this repository
for the Raylib 2D graphics API. You can layer any renderer on top of this!

### Why Zig Build? What about CMake?

Ghostling is pure C, and Zig ships with a built-in C compiler — so
`zig build` is all you need, no extra toolchain required. The CMake
configuration is kept around for people who prefer a familiar workflow,
but it is **not actively tested** and may fall behind. When in doubt,
use `zig build`.
