# Ghostling

## Building

- Requires CMake 3.19+, Ninja, a C compiler, and Zig **0.15.2+** on PATH

### CMake (default)

- Configure: `cmake -B build -G Ninja`
- Build: `cmake --build build`
- Release build: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`
- Run: `./build/ghostling` or `.\build\ghostling.exe` (Windows)
- Clean: `cmake --build build --target clean`

### Zig (`build.zig`)

- Fetches **ghostty** via `build.zig.zon` (same git revision as `CMakeLists.txt` — keep them in sync when bumping libghostty).
- Generates `font_jetbrains_mono.h` with `tools/bin2header.zig` (same idea as `bin2header.cmake`).
- **Raylib** is not a Zig dependency; point the build at an install or a CMake-built static library:
  - System / MSYS2:  
    `zig build -Draylib-prefix=C:/msys64/ucrt64` (adjust to your prefix; needs `include/raylib.h` and `lib/libraylib.a` or import libs).
  - Reuse CMake FetchContent output (after `cmake --build build` once):  
    `zig build -Draylib-lib=build/_deps/raylib-build/raylib/libraylib.a -Draylib-include=build/_deps/raylib-src/src`
- On **Windows**, the default target is `native-windows-gnu` when you omit `-Dtarget`, so linking matches typical MinGW raylib and avoids Zig’s MSVC libc setup unless you opt in with `-Dtarget=x86_64-windows-msvc`.
- **Optimization**: plain `zig build` defaults to **ReleaseFast** (Debug libghostty-vt is unusably slow). Use `-Doptimize=Debug` only when you need symbols; `--release=safe` / `--release=small` still map to ReleaseSafe / ReleaseSmall.
- **libghostty-vt** is linked **dynamically** (same idea as CMake’s `ghostty-vt` imported shared library). `zig build` installs `ghostty-vt.dll` next to `ghostling.exe` under `zig-out/bin/` on Windows; on Linux/macOS the shared library goes under `zig-out/lib/` with an rpath on the exe so `zig build run` still works.
- Install prefix defaults to `zig-out/`; run: `zig build run` (after a successful `zig build`).

## Code Conventions

- C (not C++), single-file project in `main.c`
- Never put side-effect calls inside `assert()` — removed in release builds
- Comment heavily — explain *why*, not just *what*

## Updating Libghostty

- Update **both** `CMakeLists.txt` (`FetchContent` `GIT_TAG`) and `build.zig.zon` (ghostty `url` hash — run `zig build` after changing the URL and paste the suggested hash).
- Clean the CMake `build/` folder immediately to avoid stale libghostty builds
- After cleaning, perform a rebuild to test for any API changes
