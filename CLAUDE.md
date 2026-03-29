# 目的
为 winghostling 在 Windows 上正确渲染并编译

# 编译流程
使用 `C:\Scoop\apps\git\current\git-bash.exe` 为 bash

## CMake + Ninja（主流程）
```bash
cd /c/Users/cherichy/Desktop/projects/winghostling/build && PATH="/c/Scoop/apps/msys2/current/ucrt64/bin:$PATH" ninja ghostling 2>&1 && cp _deps/ghostty-src/zig-out/bin/ghostty-vt.dll .
```
最后一行把 `ghostty-vt.dll` 拷到 `build/` 目录，与 `ghostling.exe` 同目录，否则运行时会找不到动态库。

## Zig（`zig build`）
本机 UCRT64 若未安装 `mingw-w64-ucrt-x86_64-raylib`，不要用 `-Draylib-prefix`，改用 **CMake 已 Fetch 的 raylib**（需先至少配置/编译过一次 CMake，生成 `build/_deps/...`）：

```bash
cd /c/Users/cherichy/Desktop/projects/winghostling
zig build \
  -Draylib-lib=build/_deps/raylib-build/raylib/libraylib.a \
  -Draylib-include=build/_deps/raylib-src/src
```
产物在 `zig-out/bin/`：`ghostling.exe` 与 `ghostty-vt.dll` 会一起安装，一般无需再手动 `cp` DLL。

若已在 MSYS2 安装 raylib，可简化为：
`zig build -Draylib-prefix=/c/Scoop/apps/msys2/current/ucrt64`（路径按实际安装调整）。

## 字体文件
`C:\Scoop\apps\Maple-Mono-NF-CN\7.9\`
