# M13 Physics & Collision

| 项 | 值 |
|---|---|
| 层 | L2 |
| 优先级 | P0 |
| 状态 | 设计完成 |
| 依赖 | M00–M12 |
| 被依赖 | M16、M17、M19、M20、M21、M26、M27 |

## 1. 目的

提供 2D AABB 碰撞检测、扫掠移动求解、静态瓦片碰撞、触发器与空间查询。不做刚体动力学、旋转物理或关节。

## 2. 范围

**负责**

- `Aabb`：中心/半尺寸或 min/max，统一使用 min/max + 便捷构造。
- 碰撞层与掩码：`CollisionLayer` 位标志，实体声明属于哪层、与哪些层碰撞。
- 动态碰撞体：`Collider`（AABB、offset、isTrigger、layer/mask）。
- 静态碰撞：瓦片地图的不可行走格、静态 `StaticCollider`。
- 移动求解：轴分离扫掠（先 X 后 Y），防止穿透；支持“沿墙滑动”。
- 空间分区：均匀网格哈希，按 cell 查询候选对。
- 查询：`overlap(aabb, mask)`、`raycast(origin, dir, maxDist, mask)`、`queryCircle`。
- 触发器：重叠开始/结束事件（通过 M03 事件总线）。
- 调试绘制：碰撞盒、网格、射线（M27 使用）。

**不负责**

- 力、质量、摩擦、旋转（非目标）。
- 伤害判定（M20 使用查询结果）。
- 寻路（M21）。
- 瓦片数据本身（M12）。

## 3. 核心概念与数据结构

```cpp
struct Aabb {
    glm::vec2 min, max;
    glm::vec2 center() const;
    glm::vec2 size() const;
    bool overlaps(const Aabb& other) const;
};

enum class CollisionLayer : std::uint32_t {
    None = 0, World = 1u << 0, Player = 1u << 1, Enemy = 1u << 2,
    PlayerAttack = 1u << 3, EnemyAttack = 1u << 4, Interactable = 1u << 5,
    Trigger = 1u << 6, All = 0xFFFFFFFFu
};

struct Collider {
    glm::vec2 offset{0.0f};
    glm::vec2 size{16.0f, 16.0f};
    CollisionLayer layer = CollisionLayer::World;
    CollisionLayer mask = CollisionLayer::All;
    bool isTrigger = false;
};
```

- 碰撞体是轴对齐的；旋转精灵使用不旋转的碰撞盒。
- 移动求解按固定步执行；速度为“单位/秒”，乘以固定 delta。
- 触发器不阻挡移动，只产生事件。

### 角色碰撞与动作参数（ADR-0002 / ADR-0003）

- 角色碰撞盒：脚部盒 **10×6**（脚底中心为锚点，盒顶距脚底 6px）。
- 移动：走 90 px/s，跑 150 px/s；斜向移动归一化。
- 闪避：位移 48px / 0.25s；无敌窗口 0.05–0.20s；冷却 0.5s；消耗 25 耐力。
- 近战判定盒：24×20，从脚底中心朝朝向偏移 12px 生成，判定持续 0.10s。
- 物理只处理脚部盒；排序使用 `feetWorldY`，与碰撞盒解耦。

## 4. 公共接口草案

```cpp
namespace fa::physics {

struct MoveResult {
    glm::vec2 position;
    bool hitX = false;
    bool hitY = false;
};

class IPhysicsWorld {
public:
    virtual ~IPhysicsWorld() = default;
    virtual void rebuildStatic(const world::ITilemap& map) = 0;
    virtual MoveResult move(ecs::EntityId entity, glm::vec2 delta, CollisionLayer mask) = 0;
    virtual std::vector<ecs::EntityId> overlap(const Aabb& area, CollisionLayer mask) const = 0;
    virtual std::optional<RayHit> raycast(glm::vec2 origin, glm::vec2 dir, float maxDist, CollisionLayer mask) const = 0;
    virtual void debugDraw(render::IRenderer& renderer) const = 0;
};

} // namespace fa::physics
```

## 5. 依赖关系

- 依赖 M01（数学）、M03（触发器事件）、M11（`World`/`Transform`）、M12（瓦片静态碰撞）、M06（调试绘制）。
- 被 M16/M20/M21 使用；它们只调用查询与 `move`，不直接修改位置。
- 不反向依赖玩法；攻击判定由 M20 发起查询。

## 6. 扩展点

- 新增形状（圆、胶囊）作为 `Collider` 变体。
- 新增连续碰撞（CCD）用于高速投射物；v1 用射线检测替代。
- 空间分区可替换为四叉树/BVH，接口不变。
- 新增物理材质（摩擦/弹性）为后续平台玩法预留。

## 7. 实现阶段

P0，Phase B。先实现 AABB、瓦片碰撞、轴分离移动；再实现空间哈希、触发器、射线。验收：角色不穿墙、沿墙滑动顺畅；箭矢命中检测正确；100 个敌人查询不掉帧。

## 8. 风险与开放问题

- 轴分离在斜向高速移动时仍可能穿透薄墙；限制最大速度或增加子步。
- 浮点误差导致“卡在墙里”；求解后做微小回退与 `epsilon` 容差。
- 碰撞层数量有限（32）；需要文档化分配表，避免冲突。
- 是否需要圆形碰撞体用于角色？AABB 足够，但视觉可能不匹配；可通过缩小碰撞盒缓解。
