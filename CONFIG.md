# Ghostling 配置文件说明

程序启动时会读取一个纯文本配置文件，用来设置**字体路径**和**字号**。不创建配置文件也可以运行：Windows 上会尝试使用内置的默认 Maple Mono 路径；若磁盘字体加载失败，会回退到程序内嵌的 JetBrains Mono（**不含中文大字集**，中文可能仍显示为缺字）。

---

## 配置文件放在哪里

| 系统 | 路径 |
|------|------|
| **Windows** | `%APPDATA%\ghostling\config`<br>（一般为 `C:\Users\<用户名>\AppData\Roaming\ghostling\config`） |
| **Linux / macOS** | `$HOME/.config/ghostling/config` |

请先创建目录 `ghostling`（若尚不存在），再在该目录下新建名为 `config` 的文件（**无扩展名**）。

---

## 文件格式

- 一行一个选项，形式为 **`键 = 值`**，等号两侧可以有空格。
- 以 **`#`** 开头的行视为注释；空行会被忽略。
- 键名区分大小写，请使用下文所列的英文键名。

---

## 可用选项

### `font_path`

- **含义**：终端渲染使用的 TrueType 字体文件的完整路径。
- **建议**：使用等宽、且包含你需要字符集的字体（例如含中文的 Maple Mono NF CN、Sarasa、Noto Sans Mono CJK 等）。
- **Windows 路径示例**：  
  `font_path = C:\Scoop\apps\Maple-Mono-NF-CN\7.9\MapleMono-NF-CN-Regular.ttf`
- **注意**：
  - 路径中有空格时，当前实现**不要**加引号，整段路径写在等号右侧即可（行首到行尾会先去掉首尾空白）。
  - 若路径指向的文件不存在或无法读取，程序会在标准错误输出提示，并改用**内嵌字体**（对中文支持有限）。

### `font_size`

- **含义**：逻辑字号（与屏幕 DPI 无关的“点数”基准；程序内部会再乘以窗口 DPI 去光栅化）。
- **类型**：整数。
- **范围**：`6`～`256`（超出范围的值会被忽略，仍使用默认字号）。
- **默认**：`16`（未写该项时）。

---

## 示例

**最小示例（只改字体）：**

```text
# Ghostling 配置
font_path = C:\Scoop\apps\Maple-Mono-NF-CN\7.9\MapleMono-NF-CN-Regular.ttf
```

**同时指定字号：**

```text
font_path = C:\Scoop\apps\Maple-Mono-NF-CN\7.9\MapleMono-NF-CN-Regular.ttf
font_size = 14
```

**Linux 示例：**

```text
font_path = /usr/share/fonts/opentype/noto/NotoSansMono-Regular.ttf
font_size = 16
```

---

## 与默认行为的关系（Windows）

1. 若**存在**配置文件且其中设置了 **`font_path`**（且值非空），则优先使用该路径。
2. 若配置文件不存在，或存在但未设置 `font_path`，则 Windows 构建会尝试使用编译时约定的 **Scoop Maple Mono 路径**（与 `main.c` 中 `GHOSTLING_DEFAULT_FONT_PATH` 一致；Scoop 升级版本号后若路径变了，请在配置里显式写 `font_path`）。
3. 非 Windows 系统：若未配置 `font_path`，则不会使用上述 Maple 路径，仅使用内嵌字体（适合仅需拉丁字符的场景）。

---

## 故障排查

| 现象 | 可能原因 |
|------|----------|
| 中文仍是问号 / 方块 | 当前 TTF 不含对应字形，或字体文件未成功加载（已回退到内嵌字体）。请检查 `font_path` 与文件是否存在。 |
| 启动变慢 | 程序会为终端常用 Unicode 区间生成较大字形图集，属预期行为；换用字重更大或路径异常的字体不会改善图集大小本身。 |
| 修改配置不生效 | 确认文件路径是否为 `%APPDATA%\ghostling\config`（Windows）或 `~/.config/ghostling/config`（Unix），且保存后需**重新启动**程序。 |

---

## 当前不支持的配置

以下能力尚未通过配置文件提供（将来可能会扩展）：

- 配色主题、窗口大小、shell 路径（shell 仍由命令行第一个参数指定）
- 多字体回退、彩色 Emoji 专用字体
- 热加载配置（修改后无需重启）

若你需要更多选项，可以在 issue 或 PR 中说明使用场景。
