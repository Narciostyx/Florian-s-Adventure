# M26 Editor Tools

| 项 | 值 |
|---|---|
| 层 | L4 |
| 优先级 | P2 |
| 状态 | 设计完成 |
| 依赖 | M00–M25 |
| 被依赖 | 无（叶子模块） |

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

## 4. 公共接口草案

```cpp
namespace fa::editor {

class IEditor {
public:
    virtual ~IEditor() = default;
    virtual void toggle() = 0;
    virtual bool active() const = 0;
    virtual void update(ecs::World& world, input::IInput& input) = 0;
    virtual void draw(ui::IUi& ui, render::IRenderer& renderer) = 0;
    virtual Result<void> saveMap(std::string_view path) = 0;
    virtual Result<void> loadMap(std::string_view path) = 0;
};

} // namespace fa::editor
```

## 5. 依赖关系

- 依赖 M06（绘制）、M07（输入）、M09（面板）、M11（实体）、M12（地图）、M13（碰撞可视化）、M15（内容）、M25（切换）。
- 仅被 composition root 在开发构建中实例化；发布构建不链接。
- 不反向依赖任何玩法模块的内部实现；通过公共接口操作。

## 6. 扩展点

- 新增笔刷（自动瓦片、河流、道路）作为 `IEditorTool`。
- 新增面板（任务、对话、商店）按需注册。
- 新增关卡验证器（不可达区域、缺失传送点）。
- 可拆分为独立可执行文件，与游戏共享引擎库。

## 7. 实现阶段

P2，Phase F。先实现瓦片绘制与保存；再实现对象放置、属性面板、撤销。验收：能编辑一张地图并立即在游戏中加载；撤销 50 步不崩。

## 8. 风险与开放问题

- 编辑器与运行时的数据一致性是主要风险；共用格式与校验器。
- 编辑器 UI 复杂度可能超过游戏本体；应严格限制范围。
- 是否需要独立编辑器进程？v1 建议游戏内覆盖层。
- 撤销系统对大地图全量快照的内存开销；采用命令差分。
