# M17 Interaction & Grid Placement

| 项 | 值 |
|---|---|
| 层 | L3 |
| 优先级 | P1 |
| 状态 | 设计完成 |
| 依赖 | M00–M16 |
| 被依赖 | M18、M19、M20、M25、M26 |

## 1. 目的

统一处理“玩家对着某个格子/实体做动作”的逻辑：目标选择、可达性校验、工具作用、放置物体、拾取与对话触发。避免各玩法模块各自解析鼠标与格子。

## 2. 范围

**负责**

- 目标选择：鼠标位置或朝向 + 距离 → 目标格 / 目标实体。
- 交互优先级：实体（NPC/箱子/掉落物）优先于瓦片；同格多实体按层与距离排序。
- 可达性：最大交互距离、视线/阻挡（可选）、地图边界。
- 工具动作：锄地、浇水、播种、施肥、收获、砍伐、采矿。
- 放置：家具/设备/洒水器的格子合法性校验与放置预览。
- 放置碰撞：占用格记录，防止重叠；可旋转家具的占地。
- 交互提示：高亮格、可交互图标、工具提示文本。
- 事件发布：`InteractRequested`、`ToolUsed`、`PlacementRequested`。

**不负责**

- 作物生长规则（M19）。
- 背包扣除/添加（M18）。
- 伤害判定（M20）。
- 建造 UI 布局（M09/M26）。

## 3. 核心概念与数据结构

```cpp
struct GridTarget {
    glm::ivec2 cell;
    world::TileId tile;
    ecs::EntityId entity; // 可选
    float distance;      // 世界单位
    bool valid = false;
};

enum class ToolKind { None, Hoe, WateringCan, Axe, Pickaxe, Sword, Seed, Placeable };

struct InteractionRequest {
    ecs::EntityId actor;
    GridTarget target;
    ToolKind tool;
    ContentId item; // 可为空
};
```

- 目标格由鼠标世界坐标取整；手柄/键盘用朝向格。
- 最大交互距离默认 1.5 格，工具可覆盖。
- 放置预览使用半透明 `DrawItem`，不进入 ECS。

### 瞄准与放置（ADR-0003）

- 鼠标光标 → 世界坐标 → 瓦片坐标，使用 16×16 网格。
- 高亮格子由 `TileCoord` 决定，高亮矩形为 16×16，绘制在实体之上、UI 之下。
- 放置校验顺序：区域内 → 是否可通行 → 是否已被占用 → 物品是否允许放置。
- 温室判定通过 `queryArea(tile) == AreaKind::Greenhouse`。

## 4. 公共接口草案

```cpp
namespace fa::interaction {

class IInteractionService {
public:
    virtual ~IInteractionService() = default;
    virtual GridTarget pickTarget(ecs::World& world, glm::vec2 cursorWorld, Facing facing) const = 0;
    virtual bool canInteract(const GridTarget& target, ToolKind tool) const = 0;
    virtual Result<void> interact(ecs::World& world, const InteractionRequest& request) = 0;
    virtual void drawPreview(render::IRenderer& renderer, const GridTarget& target) const = 0;
};

} // namespace fa::interaction
```

## 5. 依赖关系

- 依赖 M11（实体/组件）、M12（瓦片查询）、M13（可达性/阻挡）、M15（物品/工具定义）、M16（请求来源）、M09（提示 UI）。
- 不直接修改背包；通过事件请求 M18 扣除物品。
- 不直接实现作物逻辑；通过事件请求 M19 执行耕地/播种。

## 6. 扩展点

- 新增工具：`ToolKind` + 处理器注册表（`IToolHandler`）。
- 新增放置类型：`PlaceableDef` 描述占地、旋转、合法性规则。
- 新增交互源（NPC、按钮、触发器）：实现 `IInteractable` 组件/接口。
- 新增多格工具（镰刀 3×1）：目标从单格扩展为区域。

## 7. 实现阶段

P1，Phase C。先实现单格目标选择、锄地/浇水/播种、放置校验；再实现多格与预览。验收：鼠标与手柄都能选格；非法放置有明确反馈；交互不穿透墙体。

## 8. 风险与开放问题

- 鼠标与手柄的目标选择规则不一致，需要统一抽象。
- 交互优先级规则若过于复杂，玩家难以预期；建议固定“实体 > 作物 > 地面”。
- 放置占用格与瓦片碰撞是两套数据，需要明确谁是权威。
- 是否需要“使用工具时暂停移动”？影响手感，需原型验证。
