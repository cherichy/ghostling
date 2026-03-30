# Ghostling

## Building

- Requires CMake 3.19+, Ninja, a C compiler, and Zig **0.15.2+** on PATH

### CMake (lagecy)

- Configure: `cmake -B build -G Ninja`
- Build: `cmake --build build`
- Release build: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`
- Run: `./build/ghostling` or `.\build\ghostling.exe` (Windows)
- Clean: `cmake --build build --target clean`

### Zig (`build.zig`) — default

- Builds the **same C sources** as CMake (`src/c/*.c`), not a second implementation. Fetches **ghostty** via `build.zig.zon` (keep revision in sync with `CMakeLists.txt` when bumping libghostty).
- Generates `font_jetbrains_mono.h` with `tools/bin2header.zig` (same idea as `bin2header.cmake`).
- **Raylib (Windows / Scoop MSYS2 UCRT64)**: use this when compiling from the repo root so raylib headers and MinGW import stubs resolve correctly:  
  `zig build -Draylib-prefix="C:/Scoop/apps/msys2/current/ucrt64"`
- **Raylib (other layouts)**: `-Draylib-lib=... -Draylib-include=...` (see `zig build -h`).
- On **Windows**, default target is `native-windows-gnu` unless you pass `-Dtarget=...`.
- **Optimization**: `zig build` defaults to **ReleaseFast** for libghostty-vt; use `-Doptimize=Debug` when you need symbols.
- **libghostty-vt** is dynamic; `zig build` installs `ghostling` and the shared library under `zig-out/` (same layout idea as CMake). Run: `zig build run`.
- Step `ghostling-c` is an alias for the default install (historical name).

## Config file (optional)

Plain `key=value` lines, `#` comments. Loaded from `%APPDATA%\ghostling\config` on Windows or `~/.config/ghostling/config` on Unix (see `config_font.c`).

| Key | Meaning |
|-----|---------|
| `font_path` | TTF path (default: bundled Maple Mono path) |
| `font_size` | PTY grid size in points (default 16) |
| `tab_title_font_scale` | Tab title / `×` / `+` vs grid font: `font_size_px * this` (default `0.8`, range about 0.2–2) |
| `tab_title_h` | Pixel height of the **top** band (title + close), default `18` |
| `tab_reserved_h` | Pixel height of the **bottom** reserved band per tab, default `24` |

## Code Conventions

- C (not C++). Entry point is `src/c/main.c`; PTY, config/font, effects, and terminal UI live under `src/c/`.
- Never put side-effect calls inside `assert()` — removed in release builds
- Comment heavily — explain *why*, not just *what*

## Updating Libghostty

- Update **both** `CMakeLists.txt` (`FetchContent` `GIT_TAG`) and `build.zig.zon` (ghostty `url` hash — run `zig build` after changing the URL and paste the suggested hash).
- Clean the CMake `build/` folder immediately to avoid stale libghostty builds
- After cleaning, perform a rebuild to test for any API changes
