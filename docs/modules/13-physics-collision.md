# M13 物理与碰撞

| 项 | 值 |
|---|---|
| 层 | L2 |
| 优先级 | P0 |
| 状态 | **已实现**（`37d6b74`） |
| 依赖 | M00、M01、M04、M11（`fa_render` 仅经由 `fa_scene` 的 `ISystem` 头间接出现） |
| 被依赖 | M16、M17、M19、M20、M21、M26、M27 |
| 构建记录 | [m13-physics-collision.md](../build/m13-physics-collision.md) |

## 1. 目的

提供俯视角 2D 游戏需要的那一部分物理：**AABB 碰撞、沿墙滑动的移动求解、静态瓦片阻挡、重叠/射线查询、触发器进出事件**。

不做刚体动力学。没有质量、力矩、摩擦、弹性、关节、旋转碰撞。理由很直接：种田 + 实时动作战斗只需要"这个盒子别穿墙、贴着墙能滑动、箭能打中东西"，每多一个自由度就是一组要调、要测、要解释给美术听的数字，而像素画本身又不支持旋转。

## 2. 范围

**负责**

- `Collider` 组件：盒子的偏移、尺寸、层、掩码、是否触发器。
- `CollisionLayer`：一个实体属于哪一层、与哪些层反应；双向掩码规则。
- 静态碰撞：`ISolidTiles` 接口 + `TileGrid` 具体实现（M12 之后由真实地图实现该接口）。
- 移动求解：轴分离（先 X 后 Y）、按 ≤4 px 子步防穿透、贴墙滑动、卡住可脱出。
- 空间分区：均匀网格哈希，按 cell 取候选。
- 查询：`overlap`、`overlapCircle`、`raycast`。
- 触发器：每步对比重叠集合，产出 `Began`/`Ended` 事件。
- 调试几何：`debugDraw` 输出矩形列表，不依赖渲染器。

**不负责**

- 力、质量、摩擦、旋转、关节（非目标）。
- 伤害判定（M20 用查询结果自己算）。
- 寻路（M21）。
- 瓦片数据本身（M12）：本模块只问"这一格是不是实心"。
- 事件总线发布：本模块只**记录**触发器事件，是否发布由组装根决定（见 §10 偏离 6）。

## 3. 交付文件

| 路径 | 内容 |
|---|---|
| `src/engine/include/fa/physics/PhysicsError.hpp` | 5008–5012 错误码 |
| `src/engine/include/fa/physics/CollisionLayer.hpp` | `CollisionLayer`、`CollisionMask`、`maskOf`/`matches`/`operator\|` |
| `src/engine/include/fa/physics/Collider.hpp` | `Collider`、`colliderRect`、`layersInteract`、`kPlayerFeetCollider` |
| `src/engine/include/fa/physics/SpatialHash.hpp` | 均匀网格宽相 |
| `src/engine/include/fa/physics/TileCollision.hpp` | `ISolidTiles`、`TileGrid`、`overlapsSolidTile` |
| `src/engine/include/fa/physics/PhysicsWorld.hpp` | `Velocity`、`MoveResult`、`RayHit`、`TriggerEvent`、`DebugShape`、`PhysicsStats`、`PhysicsWorld` |
| `src/engine/include/fa/physics/PhysicsSystem.hpp` | `PhysicsSystem`（`ISystem` 适配器） |
| `src/engine/physics/src/CollisionLayer.cpp` | `toString` |
| `src/engine/physics/src/SpatialHash.cpp` | cell 范围、插入、查询、去重排序 |
| `src/engine/physics/src/TileCollision.cpp` | 网格存储、边界、`overlapsSolidTile` |
| `src/engine/physics/src/PhysicsWorld.cpp` | 求解器、宽相重建、触发器 diff、射线网格遍历 |
| `src/engine/physics/src/PhysicsSystem.cpp` | 固定步驱动 |
| `src/engine/physics/fa_physics.vcxproj` | GUID `{C0DE0009-0009-4F00-9A00-000000000009}` |
| `tests/physics.test.cpp` | 40 用例 / 299 断言 |

## 4. 核心概念

### 4.1 碰撞盒的锚点是精灵左上角，不是脚底中心

`Transform::position` 在 M11 里被定义为 **精灵左上角**（`DrawItem::position` 就是左上角，`sortY = position.y + size.y`）。物理沿用同一个定义，不另造锚点，否则同一帧里"画在哪"和"撞在哪"会各说一套。

因此"脚部盒"是通过 `offset` 摆出来的：

| 项 | 16×24 角色帧 | 推导 |
|---|---|---|
| `Transform::position` | 精灵左上角 | 与 `DrawItem` 一致 |
| 脚部盒尺寸 | 10×6 | ADR-0003 |
| `Collider::offset` | `{3, 18}` | X：左右各留 3 px；Y：`24 - 6 = 18` |
| 盒顶距脚底 | 6 px | `18 + 6 = 24`，正好落在精灵底边 |

`kPlayerFeetCollider` 就是这组常量，M16 直接拿去用即可。

**旋转与缩放不影响碰撞盒。** `Transform::rotation` / `scale` 是表现，像素画只该用 90° 倍数旋转，而缩放后的多边形和瓦片网格对不齐。如果将来真需要"变大变小的碰撞盒"，做法是改 `Collider::size`，不是让求解器去读 `scale`。

### 4.2 掩码是双向的

两个碰撞体是否反应，看两边是否都同意：

```text
layersInteract(a, b) = (a.layer & b.mask) && (b.layer & a.mask)
```

单向判定会出现"我看到你、你看不到我"的不对称行为，调试时极难定位。`CollisionMask` 是 `std::uint32_t`，游戏模块可以自行增加位而不用改头文件；但**已用的位值永不重编号**，因为它们可能进存档。

层分配表（v1）：

| 位 | 名称 | 典型用途 |
|---|---|---|
| 0 | `World` | 地形、墙、家具；瓦片隐式属于此层 |
| 1 | `Player` | 玩家脚部盒 |
| 2 | `Enemy` | 敌对单位 |
| 3 | `PlayerAttack` | 玩家近战/投射物判定 |
| 4 | `EnemyAttack` | 敌人攻击判定 |
| 5 | `Interactable` | 可交互物 |
| 6 | `Trigger` | 只报进出的体积 |

### 4.3 静态几何：只问一个 bool

`ISolidTiles` 只有三个方法：`tileSize()`、`isSolid(column, row)`，加一个供调试遍历的可选 `columns()`/`rows()`（返回 0 表示"无有限范围"，`debugDraw` 就跳过瓦片）。

M12 已通过非 owning 的 `TilemapSolids` 适配器实现这个接口，`fa_physics` 仍然不需要反向依赖 `fa_tilemap`。`TileGrid` 是这个接口的最小实现：一块 `vector<uint8_t>`，`outsideIsSolid` 默认 **true**，让有限地图天然拥有外墙。

### 4.4 宽相：整表重建，不做增量

`SpatialHash` 每次 `sync` 都 `clear()` 再全量 `insert`。在目标数量级（数百实体）下重建比维护每实体的 cell 列表更便宜，而且**不可能变陈旧**。

- cell 边界用半开区间：盒子正好停在格线上时不会侵占下一格，否则贴着墙站着的实体会永远在测试那面墙。
- `queryUnique` 排序 + 去重：**遍历顺序必须稳定**，否则固定步回放会对不上。
- `maxCellOccupancy` 是"一次查询最多多捞多少"的度量，调试卡顿时先看它。

### 4.5 求解：轴分离 + 子步

一次 `move(entity, delta)`：

1. 没有 `Collider` 或是触发器 → 直接加 `delta`（触发器不阻挡）。
2. 把 `delta` 按最长轴切成 ≤4 px 的子步（192 px/s 的冲刺在 60 Hz 下是 3.2 px，常规移动只有 1 步）。
3. 每个子步先解 X：取移动者的盒子，把它沿 X 扩成"扫掠带"，用带宽查宽相 + 覆盖到的实心瓦片，逐个算"尖端到达对方面前还能走多远"，取最小值。
4. 再解 Y，同样流程。
5. 任一轴被截短 → `hitX`/`hitY`，并累加 `blockedMoves`。

几个刻意的选择：

- **已重叠的阻挡物被忽略**（间隙小于 `-1e-3`）。卡在墙里的实体必须能走出来，否则一次浮点误差就变成永久监禁。
- **正好贴住不算重叠**：`core::Rect` 是半开的，停在 `gap == 0` 的接触点上，下一帧 `gap` 仍是 0，于是稳定停住，不会抖动也不会陷进去。
- **实体对实体使用步首快照**：一步之内两个实体撞进同一格可能重叠一帧。静态几何（种田游戏真正依赖的部分）是精确的；实体互推留作后续（§9）。

### 4.6 触发器：集合差分

每步末尾重建宽相，然后对每个触发器盒子查候选、做精确 `rectOverlaps`、把 `(trigger, other)` 收进有序列表；与上一步的列表做归并差分：

- 新增的对 → `Began`
- 消失的对 → `Ended`

因为两侧都排序去重，事件顺序**完全确定**。两个都是触发器时会产生两条事件（A 看到 B、B 看到 A），这是有意的：两边都该收到通知。

## 5. 公共接口

### 5.1 `Collider`

| 字段 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `offset` | `Vec2` | `{0,0}` | 相对 `Transform::position`（左上角） |
| `size` | `Vec2` | `{16,16}` | 世界像素 |
| `layer` | `CollisionLayer` | `World` | 单个层 |
| `mask` | `CollisionMask` | `All` | 参与反应的层集合 |
| `isTrigger` | `bool` | `false` | 只报重叠、不阻挡 |

辅助函数：

| 函数 | 语义 |
|---|---|
| `colliderRect(transform, collider)` | 世界空间盒子；不含旋转/缩放 |
| `blocksMovement(collider)` | `!isTrigger` |
| `layersInteract(a, b)` | 双向掩码判定 |

### 5.2 `PhysicsWorld`

| 方法 | 语义 |
|---|---|
| `setTiles(const ISolidTiles*)` / `tiles()` | 借用静态几何；传 `nullptr` 表示无瓦片碰撞 |
| `setCellSize(float)` | 改 cell 边长并重建索引 |
| `sync(const World&)` | 从全部 `Transform`+`Collider` 重建宽相；`step`/`move` 会自己做 |
| `move(World&, EntityId, Vec2) → MoveResult` | 移动一个实体，沿墙滑动；只写 `Transform::position` |
| `step(World&, float dt)` | 积分所有 `Velocity`，再刷新触发器事件 |
| `overlap(const World&, Rect, mask) → vector<EntityId>` | 排序后的重叠实体 |
| `overlapCircle(const World&, center, radius, mask)` | 宽相 + 精确圆-盒测试 |
| `raycast(const World&, origin, dir, maxDistance, mask) → optional<RayHit>` | 瓦片用网格遍历，实体用 slab 测试，取最近 |
| `triggerEvents() → span<const TriggerEvent>` | 最近一步产生的事件 |
| `debugDraw(const World&, vector<DebugShape>&)` | 碰撞盒 + 实心瓦片 |
| `stats() → const PhysicsStats&` | 计数器快照 |
| `clear()` | 清索引、触发器历史、全部计数器 |

`PhysicsWorld` **不可拷贝、不可移动**：它持有索引与上一步的重叠集合，复制会悄悄产生两份历史。

### 5.3 数据结构字段

| 类型 | 字段 |
|---|---|
| `MoveResult` | `position`、`hitX`、`hitY`、`substeps` |
| `RayHit` | `entity`（瓦片时无效）、`point`、`normal`、`distance`、`hitTile` |
| `TriggerEvent` | `trigger`、`other`、`phase`（`Began`/`Ended`） |
| `DebugShape` | `rect`、`color` |
| `Velocity` | `linear`（世界像素/秒） |
| `PhysicsStats` | `bodies`、`triggerBodies`、`cells`、`maxCellOccupancy`、`steps`、`moves`、`blockedMoves`、`overlapQueries`、`rayQueries`、`triggerBegins`、`triggerEnds`、`triggerEvents` |
| `SpatialHash` 字段/方法 | `cellCount()`、`entryCount()`、`maxCellOccupancy()`、`rebuilds()`、`queries()`、`rangeOf(area)` |

### 5.4 `PhysicsSystem`

| 成员 | 语义 |
|---|---|
| `PhysicsSystem(const ISolidTiles* tiles = nullptr, float cellSize = 64)` | 借用地形；指针必须活得比系统久 |
| `name()` | `"PhysicsSystem"` |
| `onFixedUpdate` | `physics().step(world, context.fixedDeltaSeconds)` |
| `physics()` | 拿到内部 `PhysicsWorld` 做查询 |
| `triggerEvents()` | 转发最近一步的事件 |

### 5.5 错误码

`fa/physics/PhysicsError.hpp`，占用 5000–5999 的 5008 起（M11 用了 5000–5007）：

| 常量 | 值 | 含义 |
|---|---|---|
| `kInvalidCollider` | 5008 | 碰撞体描述不可用 |
| `kInvalidTileSize` | 5009 | 瓦片边长或范围非正 |
| `kNoTileProvider` | 5010 | 需要地形却没配置 |
| `kMissingTransform` | 5011 | 要求移动没有 Transform 的实体 |
| `kIndexNotSynced` | 5012 | 未 `sync` 就查询宽相 |

当前实现里这些是**文档化的约定**：`move` 遇到没有 Transform 的实体直接返回空结果，不返回 `Error`（它用 `MoveResult` 而非 `Result<T>`，因为"没动"是正常情况而非异常）。

## 6. 使用范式

### 6.1 在场景里接入

```cpp
class FarmScene final : public fa::scene::BasicScene {
public:
    FarmScene() : BasicScene("farm") {}

    void onEnter() override {
        auto physics = std::make_unique<fa::physics::PhysicsSystem>(&m_tiles, 64.0f);
        m_physics = physics.get();          // 记下来供查询用
        systems().add(std::move(physics));
        systems().add(std::make_unique<MovePlayerSystem>());   // 写 Velocity
        systems().add(std::make_unique<fa::scene::SpriteExtractSystem>());
    }

    void onExit() override {
        systems().clear();                  // 系统借用 m_tiles，必须先死
        m_physics = nullptr;
    }

private:
    fa::physics::TileGrid m_tiles;          // onExit 保证系统先于它销毁
    fa::physics::PhysicsSystem* m_physics = nullptr;
};
```

**系统顺序是契约**：控制器（写 `Velocity`）→ `PhysicsSystem`（写位置）→ 提取/表现（读位置）。顺序反了，画面会慢一帧。

### 6.2 控制器只写意图

```cpp
void MovePlayerSystem::onFixedUpdate(World& world, const FrameContext&, CommandBuffer&) {
    world.each<fa::scene::Transform, fa::physics::Velocity, fa::physics::Collider>(
        [](EntityId id, fa::scene::Transform&, fa::physics::Velocity& velocity, fa::physics::Collider&) {
            if (id != m_player) return;
            velocity.linear = m_axis * kRunSpeed;   // 不碰 position
        });
}
```

位置只有求解器写。两个地方同时改位置，斜向撞墙时的结果会取决于系统注册顺序，这是最难复现的一类 bug。

### 6.3 攻击判定用查询，不用自己算盒子

```cpp
const Rect reach{feetCenter.x - 12.0f, feetCenter.y - 20.0f, 24.0f, 20.0f};
for (const EntityId hit : physics.overlap(world, reach, maskOf(CollisionLayer::Enemy))) {
    // 伤害逻辑属于 M20
}

// 箭矢：射线比逐帧小盒子更可靠
if (const auto hit = physics.raycast(world, origin, direction, 240.0f, maskOf(CollisionLayer::Enemy))) {
    // hit->entity / hit->point / hit->normal / hit->distance
}
```

### 6.4 触发器事件

```cpp
for (const fa::physics::TriggerEvent& event : m_physics->triggerEvents()) {
    if (event.phase == fa::physics::TriggerPhase::Began) {
        // 玩家走进商店门口
    }
}
```

需要让其它模块知道时，由组装根转成 M03 事件发布——物理层不发布。

### 6.5 调试绘制

```cpp
std::vector<fa::physics::DebugShape> shapes;
m_physics->physics().debugDraw(world, shapes);
for (const fa::physics::DebugShape& shape : shapes) {
    renderer->drawDebugRect(shape.rect, shape.color);
}
```

## 7. 契约要点

| # | 规则 |
|---|---|
| 1 | 只有 `PhysicsWorld` 写 `Transform::position`；其它系统只写 `Velocity` |
| 2 | `PhysicsSystem` 必须排在控制器之后、表现之前 |
| 3 | `Collider::offset` 相对精灵左上角，不是脚底中心 |
| 4 | 旋转与缩放不影响碰撞盒 |
| 5 | 掩码是双向的，两边都要点名对方 |
| 6 | 触发器永远不阻挡移动 |
| 7 | `mask` 不含 `World` 的碰撞体不碰瓦片（飞行单位的正当做法） |
| 8 | 已重叠的阻挡物被忽略，卡住可以脱出 |
| 9 | 恰好贴住不算重叠，停住不抖 |
| 10 | 快速移动自动切 ≤4 px 子步，不需要调用方限速 |
| 11 | 实体对实体使用步首快照，可能重叠一帧（已知取舍） |
| 12 | 查询前必须有 `sync`；`step`/`move` 会自己做，直接调 `overlap`/`raycast` 时要先 `sync` |
| 13 | `overlap`/`raycast` 用单向掩码（查询语义），与移动的双向规则不同 |
| 14 | 瓦片隐式属于 `World` 层；`raycastTiles` 不受 `mask` 过滤 |
| 15 | 宽相可以多捞，窄相必须精确 |
| 16 | 遍历顺序稳定：`queryUnique` 排序去重，触发器集合排序后归并 |
| 17 | `PhysicsWorld` 不可拷贝不可移动 |
| 18 | 系统借用 `ISolidTiles*`，地形必须活得比系统久 |
| 19 | 射线 `direction` 无需归一化，`distance` 始终是世界像素 |
| 20 | 射线起点在实心格内 → `distance == 0`、`hitTile == true` |

## 8. 验收

### 8.1 测试

| 范围 | 用例 | 断言 |
|---|---|---|
| `[physics]` | 40 | 299 |
| `~[physics]` | 293 | 17378 |
| 全量 | **333** | **17677** |

两个标签相加正好等于全量，说明没有用例漏打标签。三个配置（Debug / Development / Release）结果一致。

覆盖点：层/掩码/名称、盒子几何与 ADR 度量、触发器的 `blocksMovement`、双向掩码、宽相插入与去重排序、cell 边界、重建计数、`TileGrid` 边界与边框、半开区间重叠、`sync` 计数、自由移动、无碰撞体传送、撞墙贴住、单轴阻挡仍可滑另一轴、地板阻挡、高速不穿透（`substeps == 100`）、触发器不阻挡、双向掩码阻挡、忽略瓦片、卡住可脱出、`overlap` 过滤、圆查询角点、射线命中瓦片/起点在墙内/最近实体/掩码过滤/退化输入/方向归一化、`step` 积分与阻挡、零 delta、触发器进出各一次、层不匹配不触发、调试几何、计数器、`clear`、`PhysicsSystem` 固定步、确定性重跑、100 实体。

### 8.2 冒烟（`fa_app.exe --frames 60`）

三配置 exit 0。Debug/Release 输出：

```text
       920 ground tile(s), 143 solid, marker at (344, 224)
  m13: 2 collider(s), 1 trigger, 60 move(s), 26 blocked, 1 cell(s) (worst bucket 2)
       60 step(s), 0 overlap quer(ies), 1 ray(s), 1 trigger begin(s) / 1 end(s), 0 event(s) last step
       nearest wall straight ahead: 4 px
```

数字怎么对上：

| 观测 | 解释 |
|---|---|
| 922 实体 / 1846 组件 | 920 地面 × 2 (`Transform`+`Sprite`) + 标记 4 + 触发器 2 |
| 921 提取项 | 触发器没有 `Sprite`，不产生绘制项 |
| 143 实心格 | 墙列 23 + 边框 122 − 重叠 2 |
| 标记停在 `(344, 224)` | 墙列 x = 22×16 = 352，标记宽 8 → `352 − 8 = 344`；Y 由 `200 + 0.2×120×1s` 得 224 |
| `blocked = 26` | 到位后剩下的步数都在顶墙 |
| `1 begin / 1 end` | 标记穿过 x 330..338 的触发体积，最终停在 344 已离开 |
| `probe = 4 px` | 射线从 `344 + 4 = 348` 出发，墙在 352 |
| `worst bucket 2` | 两个碰撞体落在同一个 64 px cell |

**Development 可能多跑一步**（`61 step(s)`，标记 `(344, 224.4)`，`blocked = 27`）。这是 M04 墙钟积累导致的一次追帧，不是物理差异：x 被墙夹死在 344 完全稳定，只有 Y 每多一步多 0.4 px。重复运行实测两种取值都出现过。

## 9. 风险与开放问题

1. **实体对实体只有步首快照**：一步之内两个实体挤进同一格会重叠一帧。若 M20 出现"被怪推着穿墙"，需要加入实体对实体解算或分轴双向求解。
2. **卡住即忽略的副作用**：如果实体的生成位置本来就在墙里，它会一直能穿那面墙，直到离开。放置型玩法（M17）要把生成点校验纳入流程。
3. **子步上限是隐式的**：`delta` 越长子步越多，一次 `move` 的代价线性上升。极端击退（如 1000 px）会切 250 个子步，需要时应在调用侧限制位移。
4. **触发器是 O(触发器数 × 候选数)**：每步全量重算。触发器数量多到几百时需要改成事件驱动的集合增量。
5. **宽相 cell 尺寸未自适应**：64 px 对 16 px 瓦片的场景够用；如果出现大量超大碰撞体（房屋整体盒），`maxCellOccupancy` 会升高，需要按区域调 `setCellSize`。
6. **没有圆形/胶囊碰撞体**：角色用 AABB 在斜向贴墙时会有轻微"卡角"感，靠缩小盒子缓解。
7. **`raycastTiles` 不经过掩码**：瓦片永远算 `World`。若将来有"可穿透地形"的攻击，需要给 `raycast` 增加一个 tile 开关。
8. **未处理坐标极值**：`static_cast<int>(std::floor(...))` 在超大坐标下会溢出。世界尺寸限制在几千像素内时无影响。
9. **每步有少量堆分配**：`queryUnique` 返回新 `vector`，`step` 每帧一次、`move` 每个实体每步一次。100 实体时实测无感，但这是可优化点（预留 scratch 缓冲）。
10. **`PhysicsError` 目前只是文档**：没有 `Result` 返回值，因为移动失败是常态而非异常。若将来需要严格错误上报，要统一改签名。
11. **与存档的关系未定**：`Velocity` 要不要进存档？速度是意图，重进游戏应归零；`Collider` 是配置，应由内容库提供。留给 M14 决定。

## 10. 与草案的偏离

草案（本文件旧版 §3/§4）与本实现的差异，逐条说明理由。这些偏离同样记录在 [构建记录](../build/m13-physics-collision.md) 的决策表里。

| # | 草案 | 实现 | 理由 |
|---|---|---|---|
| 1 | 新增 `Aabb` 类型（min/max） | 复用 `core::Rect`（左上角 + 尺寸） | ADR-0004 已把 `Rect` 确定为唯一矩形类型；再引入 `Aabb` 会造成两套转换与两种半开约定 |
| 2 | `Collider` 以脚底中心为锚点 | 以精灵左上角为锚点，脚部盒靠 `offset` 摆出 | M11 的 `DrawItem::position` 与 `sortY` 都以左上角为准；同一帧里"画在哪"和"撞在哪"必须同源 |
| 3 | `IPhysicsWorld` 虚接口，内部持有 world | 去掉接口，`PhysicsWorld` 每次调用传 `const World&` | 不存 `World&` 就没有悬空引用；v1 只有一个实现，虚接口只会增加测试装配成本 |
| 4 | `rebuildStatic(const world::ITilemap&)` | `setTiles(const ISolidTiles*)` | M12 未实现，物理不能反向依赖它；一个 bool 查询接口足以解耦 |
| 5 | `debugDraw(render::IRenderer&)` | `debugDraw(const World&, vector<DebugShape>&)` | 产出纯数据即可无 GL 上下文测试；本模块因此完全不需要 `IRenderer` |
| 6 | 触发器走 M03 事件总线 | 每步记录 `triggerEvents()`，由组装根决定是否发布 | 去掉对 `fa_events` 的依赖，测试可以直接断言事件序列；发布策略属于组装根 |
| 7 | `Collider::mask` 类型为 `CollisionLayer` | `CollisionMask`（uint32） | 掩码天然是"多个层的集合"，用单值枚举表达会逼出 `static_cast` |
| 8 | 依赖列写 `M00–M12` | 实际依赖 `M00、M01、M04、M11` | 只 include 自己真正需要的模块；`fa_render` 只经 `fa_scene` 间接出现，所以 `fa_physics.vcxproj` 里**没有** `fa_render` 的 ProjectReference（见依赖规则） |
| 9 | `queryCircle` | `overlapCircle` | 与 `overlap` 同名族，命名一致 |
