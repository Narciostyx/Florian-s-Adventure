# M09 UI & Text

| 项 | 值 |
|---|---|
| 层 | L1 |
| 优先级 | P1 |
| 状态 | 已实现 |
| 依赖 | M01、M06（`RenderQueue`/`TextureHandle`/`DrawItem`） |
| 被依赖 | M16、M18、M20、M23、M24、M25、M26、M27 |

## 1. 目的

提供文字测量/绘制与即时模式（immediate-mode）UI 内核，覆盖 HUD、背包、菜单、对话框、
设置界面与编辑器面板。UI 不进入 ECS，不参与世界模拟；它只把本帧的绘制项追加到调用方的
`RenderQueue`，由渲染器统一排序与批处理。

v1 的目标是**先让字出现**：内置 3×5 位图字体无需任何美术文件，因此在真实字体/图集管线
落地前，HUD、调试数字和按钮标签都能显示。中文界面落地后，组装根优先加载**离线烘焙**的
图集（`assets/fonts/ui_atlas.png` + `.json`），读不到时退回内置 ASCII 字体。

## 2. 范围

**已实现**

- 字体图集：内置 3×5 ASCII 位图字体（码点 32–126），启动时程序化生成 RGBA8 像素；
  扩展字形放在 `FontAtlas::extended`（按码点升序，`glyphFor` 二分查找），来源是组装根
  解析的 `assets/fonts/ui_atlas.json`。
- 文字解码：UTF-8 逐码点推进（`nextCodepoint`），非法/截断序列退回单字节，不会越界读。
- 逐字形度量：`Glyph::bearingY` 是行框顶到字形顶的偏移，5×7 拉丁与 10×9 汉字共用一行基线。
- 字形查询：小写归一到大写；图集外码点回退到 `?`；不可用图集返回未定义字形。
- 文字排版：按字符换行（`maxWidth`）、显式换行符、左/中/右对齐、整数倍缩放。
- 文字效果：一像素投影（`shadow`）与四像素描边（`outline`，优先于投影）。
- 文字绘制：每字形一个 `DrawItem`，统一 `RenderLayer::UI`，可直接进入 `RenderQueue`。
- 控件：面板、边框、进度条、文本、按钮、复选框、滑条。
- 状态：`UiId` 名字哈希；按钮/复选框的按下—释放完成语义；滑条拖动与范围夹取。
- 主题：面板/边框/文字/按钮/强调色/轨道色与文字缩放，全部是纯值。
- 原点：`setOrigin` 把虚拟坐标整体平移到世界坐标（通常是相机可视区左上角）。
- 组装根集成：app 上传字体与 1×1 白色纹理，绘制 HUD、按钮、复选框与滑条。

**后续接入（本模块不负责）**

- 世界空间 UI（血条、伤害数字）：M06 的 `DrawItem` + M20。
- 运行时字体文件解析、动态图集、字形缓存：字形现在由 `tools/build_font_atlas.py` 离线烘焙，
  M09 仍只消费 `FontAtlas`；换成 stb_truetype/FreeType 运行时烘焙时接口不变
  （注意 stb_truetype 的字形 hinting 尚未实现，像素字体不能用它在线烘焙）。
- 布局组（水平/垂直/网格）、锚点、尺寸约束：v1 使用显式矩形。
- 键盘/手柄焦点链、Tab 顺序、文本输入框、下拉框、滚动区：后续控件。
- UI 激活时压入 `InputContext::UI`：由调用方决定；v1 冒烟只读原始鼠标状态。
- 主题 JSON、九宫格边框、存档界面：M25/M26 接入。
- 音频反馈（按钮音效）：M08。

## 3. 核心概念与数据结构

命名空间 `fa::ui`。

```cpp
struct Glyph {
    core::Rect source{};   // 图集像素矩形
    float advance = 0.0f;  // scale 1 下的前进量
    bool defined = false;  // false 仅用于不可用图集
};

struct FontAtlas {
    render::TextureHandle texture; // 由调用方上传后绑定
    int width, height;
    int glyphWidth = 3, glyphHeight = 5;
    float lineHeight = 7.0f;
    std::vector<Glyph> glyphs;        // ASCII 32..126，索引 = 码点 - 32
    std::vector<std::uint8_t> pixels; // 紧排 RGBA8，供上传
};

enum class TextAlign : std::uint8_t { Left, Center, Right };

struct TextStyle {
    core::Color color{1, 1, 1, 1};
    float scale = 1.0f;        // 向下取整，最小 1
    TextAlign align = TextAlign::Left;
    bool shadow = false;
    bool outline = false;
};

struct TextMetrics { float width, height; int lines; };

struct UiId { std::uint32_t value; static UiId fromName(std::string_view); };

struct UiInputState {
    core::Vec2 mousePosition; // 虚拟像素
    bool mouseDown, mousePressed, mouseReleased;
    bool upPressed, downPressed, activatePressed; // 预留给键盘/手柄
};

struct UiResult { bool hovered, pressed, changed; };

struct UiTheme {
    core::Color panel, panelBorder, text, textDisabled;
    core::Color button, buttonHover, buttonPressed, accent, track;
    float textScale = 2.0f;
    float padding = 4.0f;
};
```

`UiContext` 持有字体引用、白色纹理句柄、主题、本帧输入、原点、活动控件 id 与本帧绘制队列。
它不拥有 GPU 资源：字体纹理与白纹理都由组装根创建/销毁。

## 4. 公共接口

```cpp
// Font.hpp
FontAtlas buildBuiltinFont();
const Glyph& glyphFor(const FontAtlas& font, char32_t codepoint) noexcept;

// Text.hpp
TextMetrics measureText(const FontAtlas& font, std::string_view text, float maxWidth,
                        const TextStyle& style = {});
void drawText(const FontAtlas& font, std::string_view text, core::Rect rect,
              const TextStyle& style, render::RenderQueue& queue);

// Ui.hpp
class UiContext {
public:
    UiContext(const FontAtlas& font, render::TextureHandle whiteTexture, UiTheme theme = {});
    void setOrigin(core::Vec2 origin) noexcept;
    void setTheme(const UiTheme& theme) noexcept;

    void beginFrame(const UiInputState& input) noexcept;
    void endFrame() noexcept;
    void panel(core::Rect rect, core::Color color);
    void icon(core::Rect rect, render::TextureHandle texture, core::Color tint = {1,1,1,1});
    void border(core::Rect rect, core::Color color, float thickness = 1.0f);
    void progressBar(core::Rect rect, float value, float minimum, float maximum, core::Color fill);
    void text(std::string_view text, core::Rect rect, const TextStyle& style = {});

    UiResult button(UiId id, std::string_view label, core::Rect rect);
    UiResult checkbox(UiId id, std::string_view label, bool& value, core::Rect rect);
    UiResult slider(UiId id, float& value, float minimum, float maximum, core::Rect rect);

    void submit(render::RenderQueue& out) const;
    std::size_t itemCount() const noexcept;
    const FontAtlas& font() const noexcept;
};
```

调用契约：

1. 组装根上传 `FontAtlas::pixels`（`TextureFormat::Rgba8`）并写回 `FontAtlas::texture`；
2. 每帧填 `UiInputState`（鼠标位置用虚拟像素，按键用 `MouseDevice` 的 held/pressed/released）；
3. `setOrigin(相机可视区左上角)` → `beginFrame` → 控件调用 → `submit(renderQueue)`；
4. 渲染器排序后，`RenderLayer::UI` 的项最后绘制。UI 层内部**按提交顺序**绘制（不参与
   `sortY` 深度排序），所以「先画面板、再画其上的文字」就是正确的层叠关系；控件的文字
   矩形由 `UiContext` 垂直居中，若文字高度 ≥ 控件高度，居中结果会等于或高于控件顶边，
   在旧的深度排序下会被控件自己盖掉——这就是换成 10 px 中文字体后主菜单按钮「没有文字」
   的原因。现在 `theme.textScale` 用 1 倍（10 px），标题才用 2 倍。

## 5. 依赖关系

- `fa_ui` 只引用 `fa_core` 与 `fa_render`：前者提供 `Rect`/`Color`/`Vec2`，后者提供
  `RenderQueue`、`DrawItem`、`TextureHandle`、`RenderLayer`。
- **不引用** `fa_assets`、`fa_input`、`fa_scene`、`fa_events`：字体与纹理以句柄传入，
  鼠标快照由调用方提供，UI 不读 ECS 也不发事件。
- `fa_ui` **不需要 `/EHsc`**：没有 JSON、`<chrono>` 或 `<unordered_map>`。
- `UiContext` 持有 `const FontAtlas&`，调用方必须保证字体比 context 活得久；app 先声明
  `FontAtlas`，再声明 `std::unique_ptr<UiContext>`，销毁顺序因此天然正确。

## 6. 扩展点

- **新字体**：保持 `FontAtlas` 形状，替换生成/加载逻辑；`texture`、`glyphs`、`lineHeight`
  不变，`measureText`/`drawText`/`UiContext` 无需修改。
- **离线烘焙的中文图集**：`tools/build_font_atlas.py` 扫描 `src/`、`data/` 里出现的所有非 ASCII
  字符，用 FreeType 在设计字号（10px）把它们栅格化并装箱，产出 `assets/fonts/ui_atlas.png`
  （RGBA，白色字形 + alpha）与同目录 `ui_atlas.json`（逐字形 `cp`/`src`/`advance`/`bearing_y`
  与 `line_height`）。解析器在组装根（`src/app/UiFontAtlas.cpp`），因为 `fa_ui` 刻意不依赖
  `IFileSystem` 与 JSON。改了界面文案就要重新烘焙，否则新汉字会画成 `?`：

  ```powershell
  python tools/build_font_atlas.py --font <fusion-pixel-10px-monospaced-zh_hans.ttf> --size 10
  ```
- **新控件**：在 `UiContext` 增加方法，遵守 `UiResult` 的 hovered/pressed/changed 语义；
  内部用 `quad`/`text`/`border` 组装。
- **新布局**：在调用方计算矩形即可；未来的布局组可作为独立层，不改变控件签名。
- **主题/皮肤**：`UiTheme` 是纯值，调用方可在运行时替换；JSON 加载属于 M05/M25。
- **键盘/手柄导航**：`UiInputState` 已预留 `upPressed`/`downPressed`/`activatePressed`，
  焦点链可在 `UiContext` 内实现而不改公开类型。

## 7. 实现阶段

P1，Phase D。v1 已交付文字、面板、按钮、复选框、滑条与进度条，并在 app 冒烟中显示
HUD 与控件行。后续顺序建议：CJK/动态图集 → 布局组与焦点链 → 文本框/滚动区 →
存档界面（M25）与背包网格（M18）。

## 8. 风险与开放问题

1. **文字按 UTF-8 字节遍历**：v1 只支持 ASCII；多字节字符会按字节分别回退成 `?`。CJK
   需要真正的 UTF-8 解码 + 动态图集。
2. **内置 3×5 字体很小**：可读性有限，只适合占位与调试；真实字体要等字体管线。
3. **UI 排序按 `sortY`**：同一层内下方控件后绘制；重叠窗口需要显式顺序或负 `sortY`。
4. **鼠标-only**：键盘/手柄焦点链、Tab 顺序与文本输入尚未实现。
5. **无裁剪/滚动**：超出面板的控件仍会绘制；滚动区与裁剪要等布局层。
6. **无输入上下文切换**：UI 打开时不会自动压入 `InputContext::UI`；玩法输入屏蔽由
   M16/M25 的调用方决定。
7. **无主题/皮肤加载**：`UiTheme` 目前由代码设置，JSON 主题留给 M05/M25。
8. **字体纹理是单张图集**：CJK 字符数大，必须改成动态图集或位图字体子集。
9. **整数字缩放**：非整数缩放会破坏像素风；`TextStyle::scale` 向下取整，调用方仍应传整数。
