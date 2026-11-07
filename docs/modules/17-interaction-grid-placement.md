# M17 Interaction & Grid Placement

| 项 | 值 |
|---|---|
| 层 | L3 |
| 优先级 | P1 |
| 状态 | 已实现 |
| 依赖 | M00–M13（M15/M16/M09 后续接入） |
| 被依赖 | M18、M19、M20、M25、M26 |

## 1. 目的

统一处理“玩家对着某个格子/实体做动作”的逻辑：目标选择、可达性校验、工具分发、
放置校验与占用记录。避免各玩法模块各自解析鼠标/朝向与格子。

本模块只做**交互内核**：它不知道锄头、种子、斧头具体做什么，也不知道背包里有什么。
M19/M20 通过 `IToolHandler` 注册具体行为；M16 负责何时调用；M09 负责提示 UI。

## 2. 范围

**已实现**

- 目标选择：鼠标世界坐标或朝向 → 目标格 + 目标实体。
- 实体优先：`Interactable` 覆盖的实体优先于瓦片；按 `priority`、距离、`EntityId` 排序。
- 可达性：最大交互距离（默认 1.5 格）、Bresenham 视线/墙体阻挡、地图边界。
- 工具分发：`ToolKind` + `IToolHandler` 注册表；无处理器返回 `kNoHandler`。
- 放置校验：地图边界、每格可行走、占用、距离、视线、`requiredArea`（温室）。
- 占用记录：`PlacementGrid` 独立于瓦片碰撞；支持多格占地、按 owner 整体移除。
- 放置预览：`PlacementPreview` 给出覆盖矩形、合法性、错误码与颜色。
- 事件记录：`InteractRequested`、`ToolUsed`、`PlacementRequested`、`PlacementDenied`、
  `RemovalRequested`，由组装根决定是否发布到 M03。
- 冒烟集成：固定目标/工具/放置序列，输出目标格、是否实体、距离、事件数、占用格。

**后续接入（本模块不负责）**

- 具体工具效果（锄地、浇水、播种、砍伐、采矿）：M19/M20 的处理器。
- 背包扣除/添加：M18。
- 输入来源与朝向：M16。
- 提示文本/高亮动画：M09；M17 只给纯数据。
- 内容 ID：M15 已实现；M17 仍用不透明的 `std::uint32_t payload`，接入时由组装根/处理器替换。
- 作物生长、伤害判定：M19/M20。

## 3. 核心概念与数据结构

命名空间 `fa::interaction`。

```cpp
enum class Facing : std::uint8_t { North, East, South, West, Count };
enum class ToolKind : std::uint8_t {
    None, Hoe, WateringCan, Axe, Pickaxe, Sword, Seed, Harvest, Placeable, Count
};

struct GridTarget {
    tilemap::TileCoord cell;
    tilemap::TileId tile;
    scene::EntityId entity;   // invalid = tile target
    std::uint32_t payload;    // Interactable::payload
    core::Vec2 point;         // 实体中心或格子中心
    float distance;
    bool valid;
    bool reachable;
};

struct InteractionRequest {
    scene::EntityId actor;
    GridTarget target;
    ToolKind tool;
    std::uint32_t payload;    // M15 之前的临时内容 ID
};

struct PlacementRequest {
    scene::EntityId actor;
    scene::EntityId owner;
    tilemap::TileCoord cell;
    core::Vec2 size{16.0f, 16.0f};
    std::optional<tilemap::AreaKind> requiredArea;
    std::uint32_t payload;
};

struct PlacementPreview {
    core::Rect bounds;
    bool valid;
    int errorCode;
    core::Color color;
};

struct Interactable {
    core::Rect area{0, 0, 16, 16}; // 相对 Transform::position
    std::uint32_t priority = 0;
    bool enabled = true;
    std::uint32_t payload = 0;
};
```

- `Interactable` 是纯数据组件，和 `Transform` 的锚点一致（精灵左上角）。
- `payload` 取代草案里的 `ContentId`：M17 当时不依赖内容数据库；M15 现已实现，接线时由组装根/处理器替换。
- `GridTarget::point` 对实体是实体中心、对瓦片是格子中心；距离按中心到中心计算。
- 目标格用 `tilemap::TileCoord`；世界坐标 ↔ 格子仍由 M12 负责。

## 4. 公共接口

```cpp
class PlacementGrid {
public:
    core::Status resize(int columns, int rows);
    bool contains(int column, int row) const;
    bool isOccupied(int column, int row) const;
    scene::EntityId ownerAt(int column, int row) const;
    std::uint32_t payloadAt(int column, int row) const;
    static int cellsAlong(float size, int tileSize);
    static core::Rect footprintRect(const Tilemap&, TileCoord origin, core::Vec2 size);
    bool canPlace(const Tilemap&, TileCoord origin, core::Vec2 size) const;
    bool place(const Tilemap&, TileCoord origin, core::Vec2 size, EntityId owner,
               std::uint32_t payload = 0);
    std::size_t remove(EntityId owner);
    void clear();
    std::size_t occupiedCells() const;
};

class InteractionService {
public:
    InteractionService(const Tilemap& map, PlacementGrid* placement = nullptr);

    GridTarget pickTarget(const World&, EntityId actor, core::Vec2 cursorWorld) const;
    GridTarget pickTargetInFront(const World&, EntityId actor, Facing facing) const;
    bool canInteract(const World&, EntityId actor, const GridTarget&, ToolKind) const;
    core::Status interact(World&, const InteractionRequest&);

    core::Status canPlace(const World&, const PlacementRequest&) const;
    PlacementPreview previewPlacement(const World&, const PlacementRequest&) const;
    core::Status requestPlacement(World&, const PlacementRequest&);
    core::Status requestRemoval(World&, EntityId actor, TileCoord cell);

    void addHandler(IToolHandler&);
    std::span<const InteractionEvent> events() const;
    static bool hasLineOfSight(const Tilemap&, TileCoord from, TileCoord to);
};

class IToolHandler {
public:
    virtual bool handles(ToolKind) const noexcept = 0;
    virtual core::Status use(const ToolUseContext&) = 0;
};
```

### 目标选择规则

1. `pickTarget` 先把光标转成格子；不在 map 内直接 `valid = false`。
2. 光标落在某个 `Interactable` 的局部矩形内时，实体优先于瓦片。
3. 多实体按 `priority` 降序、距离升序、`EntityId` 升序决定。
4. `pickTargetInFront` 用 actor 中心格 + 朝向偏移，再复用同一套规则。
5. `reachable = 距离 ≤ maxDistance && 视线不被中间墙格挡住`。
6. `target.entity` 无效时回退到瓦片；`tile` 仍来自 `Tilemap::tileAt`。

### 交互校验规则

- actor 必须存活且有 `Transform`。
- target 必须在 map 内；实体目标还要 `Interactable` 存在且 `enabled`。
- 距离超过 `maxDistance` → `kOutOfRange`。
- 中间有实心格 → `kBlocked`；目标格本身可以是墙（`Pickaxe`/`Axe`/`Sword`）。
- 瓦片目标必须先有 tile id；空瓦片 → `kInvalidTarget`。
- 非实体目标且 `!isWalkable` 时，只有斧/镐/剑可以继续；其他工具 → `kBlocked`。

### 放置校验顺序

1. 有 `PlacementGrid`。
2. actor 与 owner 存活。
3. 尺寸能转换成至少 1×1 的格子，且覆盖范围不越过地图/占用网格。
4. 距离与视线（与交互相同）。
5. 每一格 `isWalkable`。
6. 每一格未被占用。
7. 设置了 `requiredArea` 时，每一格的 `queryArea` 必须匹配。

放置成功只记录占用，不创建实体；`owner` 由调用方（M19）创建并负责生命周期。
`requestRemoval` 会移除该 owner 名下的**所有**占用格。

## 5. 依赖关系

- 直接引用 `fa_core`、`fa_scene`、`fa_physics`、`fa_tilemap`。
- **不引用 `fa_assets`、`fa_events`、`fa_input`、`fa_content`**。
  - 事件按调用记录在 `events()`，由 composition root 决定是否发到 M03。
  - 内容 ID 用不透明 `payload`，等 M15 落地后替换。
  - 输入由 M16/M09 提供，M17 只接受光标点或朝向。
- 不直接修改 `Transform::position`；移动仍归 M13。
- 不拥有实体/组件；`Interactable` 是普通组件，由 M11 容器管理。
- `PlacementGrid` 与瓦片碰撞是两套数据：前者是放置占用，后者是地形阻挡。
  谁更权威由调用方决定；M19 放置时应同时创建碰撞体或更新地图属性。

## 6. 实现与测试

```text
src/engine/include/fa/interaction/   InteractionTypes, InteractionService,
                                     PlacementGrid, Interactable, ToolHandler,
                                     InteractionError
src/engine/interaction/src/          InteractionService.cpp, PlacementGrid.cpp,
                                     InteractionTypes.cpp
```

测试：`tests/interaction.test.cpp`，15 个用例 / 184 条断言，覆盖：

- `Facing`/`ToolKind`/事件名；
- `PlacementGrid` 尺寸、footprint、多格占用、重叠拒绝、按 owner 移除；
- 实体优先级、瓦片回退、不可达、地图外、朝向目标；
- 视线穿墙、目标格为墙、对角墙角；
- `canInteract` 的瓦片/实体规则与工具限制；
- 工具分发、重复注册、无处理器、payload/目标传递；
- 放置边界、不可行走、占用、尺寸、owner 存活、`requiredArea`；
- 放置/移除事件与占用计数；
- actor 中心在有/无 `Collider` 时的计算。

冒烟：`DemoScene` 在标记旁放一个可交互板条箱，并用 19,11 空格做一次放置。
Debug 实测：目标 `(19,12)`、实体、12.6 px、可达；1 次工具调用、1 次放置、
3 个事件、1 个占用格；预览 `valid -> invalid`（放置后变红）。

## 7. 扩展点

- 新工具：新增 `ToolKind` + 一个 `IToolHandler`，不需要改目标选择。
- 新放置类型：`PlacementRequest::size` 已支持多格；旋转可用交换宽高表达。
- 多格工具（镰刀 3×1）：在服务外把目标扩展成区域，或新增 `GridTarget` 的 span 版本。
- 交互源（NPC/按钮/触发器）：给实体加 `Interactable`，或让 M16 直接构造请求。
- 提示 UI：消费 `GridTarget`/`PlacementPreview`，M17 不依赖 M09。

## 8. 风险与开放问题

- `payload` 是临时内容 ID；M15 落地后要统一替换，避免出现第二套 ID。
- 放置占用与实体碰撞可能不同步；M19 必须在放置成功后创建对应碰撞体。
- 目前不支持旋转矩阵；旋转通过交换 `size` 宽高近似，非正方形家具有歧义。
- 放置不检查已有实体/碰撞体，只检查地图可走与占用网格；M19 应先清理或校验。
- 移除按 owner 清空所有格子；多格对象移除是原子的，但不会通知其他系统。
- 视线是单条中心线 + 对角墙角检查，不模拟体积；大体型 actor 可能穿过窄缝。
- `canInteract` 每次重算距离/视线，适合低频玩家操作，不适合逐帧扫大量目标。
- `Interactable` 的矩形不会随 `Transform::scale/rotation` 变换；像素风只允许 90° 旋转。
- 尚无“使用工具时暂停移动”的手感决策；由 M16 原型验证。

