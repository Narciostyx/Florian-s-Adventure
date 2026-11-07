# M26 Editor Tools

| 项 | 值 |
|---|---|
| 层 | L4 |
| 优先级 | P2 |
| 状态 | 已实现 |
| 依赖 | M00–M25（实际链接 `fa_core`、`fa_tilemap`、`fa_render`、`fa_ui`） |
| 被依赖 | 无（叶子模块） |

> 实测与踩坑：[构建记录](../build/m26-editor-tools.md)
>
> 完整可视化编辑器（左地图 / 右组件库 / 右属性检查器）设计见
> [M26 Visual Editor](26-visual-editor.md)。

## 1. 目的

提供游戏内/独立的地图与内容编辑能力，降低关卡与数值迭代成本。仅在 Debug/Development 构建启用。

## 2. 范围

**负责**

- 地图编辑器：瓦片绘制、图层切换、填充/矩形/随机笔刷、撤销/重做、地图保存。
- 对象放置：作物、世界对象、NPC 出生点、敌人刷新点、传送点。
- 属性面板：选中格子/实体的组件编辑。
- 内容检查器：浏览 `ItemDef`/`CropDef`/`EnemyDef`，查看引用与错误。
- 动画预览：播放 clip、调整帧时长、查看事件帧。
- 碰撞层可视化编辑。
- 编辑器相机（自由平移/缩放）与网格吸附。
- 保存/加载地图与场景描述（与运行时格式一致）。
- 编辑器专用输入上下文与快捷键。

**不负责**

- 美术资源制作（外部工具）。
- 运行时玩法逻辑。
- 发布构建中的任何 UI。
- 通用脚本编辑器。

## 3. 核心概念与数据结构

```cpp
struct EditorState {
    bool active = false;
    int activeLayer = 0;
    ToolKind tool = ToolKind::Brush;
    glm::ivec2 hoverCell;
    std::vector<EditorCommand> undoStack;
    std::vector<EditorCommand> redoStack;
};

struct EditorCommand {
    std::string label;
    std::function<void()> undo;
    std::function<void()> redo;
};
```

- 所有修改通过命令对象，保证撤销/重做与脏标记一致。
- 编辑器直接操作 `Tilemap` 与场景数据，不经过玩法系统。
- 保存使用与运行时相同的 JSON 格式，避免双格式。

## 4. 公共接口

实际落地是 `MapEditor`（纯编辑逻辑）+ `EditorOverlay`（激活/鼠标/面板），没有引入
`IEditor` 接口：

```cpp
namespace fa::editor {

class MapEditor {
public:
    MapEditor(tilemap::Tilemap& map, EditorHistory& history);
    core::Status applyPrimary();
    core::Status applySecondary();
    core::Status paint(tilemap::TileCoord cell, tilemap::TileId tile);
    core::Status fillRect(tilemap::TileCoord from, tilemap::TileCoord to, tilemap::TileId tile);
    core::Status floodFill(tilemap::TileCoord start, tilemap::TileId tile);
    std::vector<tilemap::TileCoord> previewCells() const;
    std::string mapText() const;
    EditorStats stats() const;
};

class EditorOverlay {
public:
    explicit EditorOverlay(tilemap::Tilemap& map);
    void toggle();
    bool active() const;
    void update(const EditorFrameInput& input);
    void draw(ui::UiContext& ui) const;
    void undo();
    void redo();
};

} // namespace fa::editor
```

保存/加载由 app 通过 M02 `IFileSystem` 调用 `mapText()` 与 M12 的
`loadTilemapFromJson()` 完成；M26 本身不链接平台层。

早期草案（未采用，保留作设计记录）：

```cpp
namespace fa::editor {

class IEditor {
public:
    virtual ~IEditor() = default;
    virtual void toggle() = 0;
    virtual bool active() const = 0;
    virtual void update(scene::World& world, input::IInput& input) = 0;
    virtual void draw(ui::IUi& ui, render::IRenderer& renderer) = 0;
    virtual Result<void> saveMap(std::string_view path) = 0;
    virtual Result<void> loadMap(std::string_view path) = 0;
};

} // namespace fa::editor
```

## 5. 依赖关系

- 设计上依赖 M06（绘制）、M07（输入）、M09（面板）、M11（实体）、M12（地图）、M13（碰撞可视化）、M15（内容）、M25（切换）。
- **实际落地**：`fa_editor` 只链接 `fa_core`、`fa_tilemap`、`fa_render`、`fa_ui`。
  v1 只编辑瓦片；对象/内容/碰撞/动画面板尚未实现。
- 仅被 composition root 在 Debug/Development 实例化：`FA_ENABLE_EDITOR` 宏 +
  `fa_app.vcxproj` 的非 Release `ProjectReference` 双重保证发布构建不链接。
- 不反向依赖任何玩法模块的内部实现；通过公共接口操作。

## 6. 扩展点

- 新增笔刷（自动瓦片、河流、道路）作为 `IEditorTool`。
- 新增面板（任务、对话、商店）按需注册。
- 新增关卡验证器（不可达区域、缺失传送点）。
- 可拆分为独立可执行文件，与游戏共享引擎库。

## 7. 实现落地

P2，Phase F，已完成 v1：

- 瓦片编辑：Brush/Rect/Fill/Erase/Picker、图层切换、矩形预览。
- 撤销/重做：命令差分、容量 128、redo 清空、脏标记；实测 50 步往返。
- 保存/加载：M12 新增 `saveTilemapToJson()`，与 `loadTilemapFromJson()` 对称；
  app 写 `saves/editor-map.json` 并读回校验。
- 覆盖层：F2 开关、1–5 工具、`[`/`]` 图层、Z/Y 撤销/重做、S/L 保存/加载、
  右上角 M09 状态面板、悬停/矩形预览框。
- 构建边界：Debug/Development 启用，Release 不链接并打印 disabled。

验收：编辑立刻在下一帧提取中可见；撤销 50 步不崩；保存文件能被运行时 loader 读取。

## 8. 风险与开放问题

- 编辑器与运行时的数据一致性是主要风险；共用格式与校验器。
- 编辑器 UI 复杂度可能超过游戏本体；应严格限制范围。
- 是否需要独立编辑器进程？v1 建议游戏内覆盖层。
- 撤销系统对大地图全量快照的内存开销；采用命令差分。
