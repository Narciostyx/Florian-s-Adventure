# M09 UI & Text

| 项 | 值 |
|---|---|
| 层 | L1 |
| 优先级 | P1 |
| 状态 | 设计完成 |
| 依赖 | M00–M07 |
| 被依赖 | M16、M18、M20、M23、M24、M25、M26、M27 |

## 1. 目的

提供即时模式（immediate-mode）UI 与文字布局能力，覆盖 HUD、背包、菜单、对话框、设置界面与编辑器面板。UI 不进入 ECS，不参与世界模拟。

## 2. 范围

**负责**

- 文字：字形图集、排版（对齐、换行、行高）、颜色、阴影/描边、逐字显示。
- 控件：标签、按钮、图标按钮、复选框、滑条、文本框、下拉框、列表/网格、滚动区、进度条、提示气泡。
- 布局：立即模式 + 显式布局组（水平/垂直/网格）、锚点、边距、尺寸约束。
- 焦点与导航：键盘/手柄焦点链、Tab 顺序、按下/悬停/禁用状态。
- 输入上下文：UI 激活时压入 `UI` 上下文，阻止玩法输入。
- 主题：颜色、字体、九宫格边框、间距，从 JSON 加载。
- 调试覆盖层基础（M27 使用）。

**不负责**

- 世界空间 UI（血条、伤害数字）；由 M06 的 `DrawItem` + M20 实现。
- 字体图集生成工具；离线生成或 M26 生成。
- 本地化系统（v1 仅中/英字符串表，接口预留）。
- 音频/存档。

### 新增负责项：HUD 与存档界面（ADR-0002）

- HUD：生命条、**耐力条**、金钱、日期/时间/季节、当前工具与选中物品。
- 闪避冷却指示（耐力不足时高亮反馈）。
- 存档/读档界面：3 个手动槽 + 1 个自动槽，显示玩家名、日期、游玩时长。
- 所有按键提示统一使用键鼠文案；预留 `BindingGlyphProvider` 接口供未来手柄使用。

## 3. 核心概念与数据结构

```cpp
struct UiRect { float x, y, w, h; };

struct TextStyle {
    glm::vec4 color{1,1,1,1};
    float scale = 1.0f;
    TextAlign align = TextAlign::Left;
    bool shadow = false;
    bool outline = false;
};

struct FontAtlas {
    TextureHandle texture;
    std::vector<Glyph> glyphs; // 码点 → UV/advance
    float lineHeight;
};

enum class UiResult { None, Pressed, Released, Hovered, Changed };
```

- 每帧重新构建 UI；`UiId` 用于状态（焦点、滚动位置、文本框内容）持久化。
- 控件返回 `UiResult`；布局组使用 `pushLayout`/`popLayout`。
- 文字排版结果缓存按 `(字符串, 宽度, 字体)` 键，避免每帧重复断行。

## 4. 公共接口草案

```cpp
namespace fa::ui {

class IUi {
public:
    virtual ~IUi() = default;
    virtual void beginFrame(input::IInput& input, const time::FrameContext& time) = 0;
    virtual void endFrame(render::IRenderer& renderer) = 0;

    virtual void text(UiId id, std::string_view text, const UiRect& rect, const TextStyle& style) = 0;
    virtual UiResult button(UiId id, std::string_view label, const UiRect& rect) = 0;
    virtual UiResult checkbox(UiId id, std::string_view label, bool& value, const UiRect& rect) = 0;
    virtual UiResult slider(UiId id, float& value, float min, float max, const UiRect& rect) = 0;
    virtual bool textField(UiId id, std::string& text, const UiRect& rect) = 0;

    virtual void pushLayout(const LayoutDesc& layout) = 0;
    virtual void popLayout() = 0;
    virtual void setFocus(UiId id) = 0;
};

} // namespace fa::ui
```

## 5. 依赖关系

- 依赖 M01（数学/哈希）、M05（字体/主题资源）、M06（`drawText`/纹理四边形）、M07（导航输入）。
- 不依赖 M11/ECS；玩法通过读取自身状态并调用 UI，不把 UI 状态放进 ECS。
- M23 对话、M25 菜单、M26 编辑器均为 UI 的调用方。

## 6. 扩展点

- 新增控件：实现 `IWidget` 或在 `IUi` 增加方法；保持 `UiResult` 语义一致。
- 新增布局算法（弹性、约束求解）作为 `LayoutDesc` 的变体。
- 新增字体：动态字形上传（CJK 必需）；v1 使用预生成位图字体。
- 新增主题/皮肤：JSON 驱动，支持运行时切换。

## 7. 实现阶段

P1，Phase D。先实现文字、按钮、面板、HUD 布局；再实现背包网格、滚动区、文本框。验收：HUD 显示生命/耐力/时间/金钱；背包可拖放；对话框逐字显示可跳过。

## 8. 风险与开放问题

- 即时模式 UI 在复杂编辑器面板下性能与状态管理容易失控；编辑器可退化为保留模式。
- 中文字形数量大，预生成图集不现实；需要动态图集 + 字体文件解析（stb_truetype）或位图字体子集。
- 像素字体缩放必须整数倍，否则模糊；主题需约束字号。
- 手柄导航与鼠标导航混合的焦点规则需要明确优先级。
