# Ghostling 配置文件说明

程序启动时会读取一个纯文本配置文件，用来设置**字体**、**字号**、**字形集合（内存/启动耗时）**和**侧栏 Tab 行高/标题缩放**等。仓库根目录提供一份 **`config.example`**（含当前支持的全部键与默认值说明），可复制到下文路径并改名为 `config`。

不创建配置文件也可以运行：会优先尝试仓库内的 **`fonts/MapleMono-NF-CN-Regular.ttf`**（相对**当前工作目录**）；若该路径不可读或加载失败，C 构建会回退到内嵌 JetBrains Mono，Zig 构建同样会回退到内嵌字体（**不含中文大字集**，中文可能仍显示为缺字）。

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
- **路径示例**（也可用任意绝对路径）：  
  `font_path = fonts/MapleMono-NF-CN-Regular.ttf`（需在仓库根目录下启动，或把 `fonts` 放在当前工作目录下）  
  `font_path = C:\path\to\MapleMono-NF-CN-Regular.ttf`
- **注意**：
  - 路径中有空格时，当前实现**不要**加引号，整段路径写在等号右侧即可（行首到行尾会先去掉首尾空白）。
  - 若路径指向的文件不存在或无法读取，程序会在标准错误输出提示，并改用**内嵌字体**（对中文支持有限）。

### `font_size`

- **含义**：逻辑字号（与屏幕 DPI 无关的“点数”基准；程序内部会再乘以窗口 DPI 去光栅化）。
- **类型**：整数。
- **范围**：`6`～`256`（超出范围的值会被忽略，仍使用默认字号）。
- **默认**：`16`（未写该项时）。

### `font_codepoint_set`

- **含义**：控制字体图集要预加载的 codepoint 集合，直接影响启动耗时和内存占用。
- **类型**：字符串（大小写不敏感）。
- **可选值**：
  - `full`：完整集合（默认，兼容旧行为，约 31k codepoints）
  - `compact`：常用 CJK + 全角 + 基础符号（约 24k codepoints）
  - `latin`：拉丁/符号/框线（不含 CJK，约 4k codepoints）
- **默认**：`full`。
- **建议**：
  - 主要跑英文工具链：优先 `latin`，内存显著下降。
  - 需要中日韩文本但不依赖冷门兼容区：选 `compact`。
  - 追求最大兼容：保留 `full`。

### `tab_title_font_scale`

- **含义**：侧栏里 Tab 标题、`×`、底部 `+` 的字号，相对 PTY 栅格字体的缩放（内部为 `font_size` 经 DPI 后的像素再乘本系数）。
- **类型**：小数。
- **范围**：约 `0.2`～`2.0`（超出会被忽略）。
- **默认**：`0.8`。

### `tab_title_h`

- **含义**：每个 tab **上半部分**高度（像素），即标题与关闭按钮所在行。
- **类型**：整数。
- **范围**：`8`～`128`。
- **默认**：`18`。

### `tab_reserved_h`

- **含义**：每个 tab **下半部分**预留高度（像素），当前为空白区，供以后扩展。
- **类型**：整数。
- **范围**：`8`～`128`。
- **默认**：`24`。

---

## 示例

**最小示例（只改字体）：**

```text
# Ghostling 配置
font_path = fonts/MapleMono-NF-CN-Regular.ttf
```

**同时指定字号：**

```text
font_path = fonts/MapleMono-NF-CN-Regular.ttf
font_size = 14
font_codepoint_set = compact
```

**Linux 示例：**

```text
font_path = /usr/share/fonts/opentype/noto/NotoSansMono-Regular.ttf
font_size = 16
```

---

## 与默认行为的关系

1. 若**存在**配置文件且其中设置了 **`font_path`**（且值非空），则优先使用该路径。
2. 若配置文件不存在，或存在但未设置 `font_path`，则使用默认 **`fonts/MapleMono-NF-CN-Regular.ttf`**（与源码中 `GHOSTLING_DEFAULT_FONT_PATH` / Zig `configLoad` 一致）。请从仓库根目录运行，或自行在配置里写绝对路径。
3. 若默认路径仍无法加载字体，则回退到内嵌 JetBrains Mono（见上文）。

---

## 故障排查

| 现象 | 可能原因 |
|------|----------|
| 中文仍是问号 / 方块 | 当前 TTF 不含对应字形，或字体文件未成功加载（已回退到内嵌字体）。请检查 `font_path` 与文件是否存在。 |
| 启动变慢 / 内存高 | 程序会按 `font_codepoint_set` 生成字形图集；`full` 集合最大。可改成 `compact` 或 `latin` 降低开销。 |
| 修改配置不生效 | 确认文件路径是否为 `%APPDATA%\ghostling\config`（Windows）或 `~/.config/ghostling/config`（Unix），且保存后需**重新启动**程序。 |

---

## 当前不支持的配置

以下能力尚未通过配置文件提供（将来可能会扩展）：

- 配色主题、窗口大小、左侧序号列宽度、shell 路径（shell 仍由命令行第一个参数指定）
- 多字体回退、彩色 Emoji 专用字体
- 热加载配置（修改后无需重启）

若你需要更多选项，可以在 issue 或 PR 中说明使用场景。
