# M20 Combat

| 项 | 值 |
|---|---|
| 层 | L3 |
| 优先级 | P1 |
| 状态 | 设计完成 |
| 依赖 | M00–M19 |
| 被依赖 | M21、M24、M25、M27 |

## 1. 目的

实现实时动作战斗：攻击判定窗口、伤害计算、受击反馈、死亡与掉落、投射物与状态效果。战斗规则数据驱动，判定与表现分离。

## 2. 范围

**负责**

- 生命/伤害：`Health`、`DamageEvent`、伤害类型（物理/元素）、防御减伤。
- 攻击：近战扇形/矩形判定窗口、连击、蓄力（预留）。
- 投射物：箭矢/魔法弹的移动、碰撞、生命周期。
- 受击：无敌帧、击退、闪烁、硬直。
- 死亡：死亡动画、掉落表结算、实体销毁。
- 状态效果：中毒、减速、燃烧；持续时间与叠加规则。
- 战斗事件：`DamageDealt`、`EntityDied`、`ProjectileHit`。
- 世界空间表现：伤害数字、命中特效（通过 M06 绘制）。
- 敌人攻击的通用执行器（由 M21 触发）。

**不负责**

- 敌人决策（M21）。
- 玩家输入与状态机（M16）。
- 掉落物定义（M15 的 `LootTableDef`）。
- 背包入包（M18）。

### 耐力与闪避（ADR-0002 决策 1）

- 耐力上限 100；不自动回满，仅在 0.6s 无消耗后以 22/s 回复。
- 消耗：轻击 15、重击 25、闪避 25。不足则拒绝动作。
- 闪避无敌窗口 0.05–0.20s，冷却 0.5s，位移 48px / 0.25s。
- 受击硬直期间无法闪避；硬直结束可立即闪避。

## 3. 核心概念与数据结构

```cpp
struct Health {
    int current = 100;
    int max = 100;
    float invulnerableUntil = 0.0f;
};

struct Damage {
    int amount = 0;
    DamageType type = DamageType::Physical;
    scene::EntityId source;
    glm::vec2 knockback{0.0f};
};

struct AttackHitbox {
    glm::vec2 offset;
    glm::vec2 size;
    float activeFrom; // 动画时间
    float activeTo;
    int damage;
    CollisionMask targetMask;   // M13 的 mask 是位集合，不是单个层
};

struct Projectile {
    glm::vec2 velocity;
    float lifetime;
    int damage;
    CollisionMask targetMask;
    scene::EntityId owner;
};
```

- 判定在固定步执行；表现（特效/数字）在可变步或渲染阶段。
- 同一攻击对同一目标只结算一次（攻击实例 ID 去重）。
- 无敌帧按时间戳判断，避免浮点累积。

### 数据结构补充（ADR-0002 决策 1）

```cpp
struct Stamina {
    float current = 100.0f;
    float max = 100.0f;
    float regenDelay = 0.6f;
    float regenRate = 22.0f;   // per second
    float sinceLastSpend = 0.0f;
};

struct DodgeState {
    bool active = false;
    float elapsed = 0.0f;
    float duration = 0.25f;
    float cooldownRemaining = 0.0f;
    Vec2 direction;
};
```

判定顺序：`canSpendStamina` → 扣减 → 启动动作；`isInvulnerable()` 由 `DodgeState` 时间窗与受击硬直共同决定。

## 4. 公共接口草案

```cpp
namespace fa::combat {

class ICombatService {
public:
    virtual ~ICombatService() = default;
    virtual void applyDamage(scene::World& world, scene::EntityId target, const Damage& damage) = 0;
    virtual scene::EntityId spawnProjectile(scene::World& world, const ProjectileDesc& desc) = 0;
    virtual void spawnAttack(scene::World& world, scene::EntityId owner, const AttackHitbox& hitbox) = 0;
    virtual void applyStatus(scene::World& world, scene::EntityId target, const StatusEffect& effect) = 0;
};

} // namespace fa::combat
```

## 5. 依赖关系

- 依赖 M03（事件）、M06（特效绘制）、M11（ECS）、M13（查询/射线）、M15（武器/敌人/掉落定义）、M18（掉落入包）。

> **M13 已实现**（见 [M13 物理与碰撞](13-physics-collision.md)）。伤害判定直接复用查询，不要自己遍历实体算盒子：
> - 近战：`PhysicsWorld::overlap(world, reach, maskOf(CollisionLayer::Enemy))`，`reach` 用 24×20 判定盒；
> - 箭矢：`PhysicsWorld::raycast(world, origin, direction, range, maskOf(CollisionLayer::Enemy))`，
>   返回 `RayHit{entity, point, normal, distance}`；瓦片也会命中（`hitTile == true`），所以箭会撞墙；
> - 击退：`PhysicsWorld::move(world, target, KnockbackDelta)` 或临时写 `Velocity`，**不要**直接改 `Transform::position`；
> - 攻击盒的 `targetMask` 是 `CollisionMask`（位集合）。
- 不依赖 M21；M21 通过 `ICombatService` 发起攻击。
- M16 只发布 `AttackRequested`，由 M20 执行。

## 6. 扩展点

- 新增伤害类型与抗性表。
- 新增攻击形状（扇形、圆形、多段）作为 `AttackHitbox` 变体。
- 新增状态效果：实现 `IStatusEffect` 与叠加策略。
- 新增暴击/元素反应：在伤害管线中插入修正器。

## 7. 实现阶段

P1，Phase D。先实现近战判定、伤害、死亡、掉落；再实现投射物、状态效果、连击。验收：攻击命中一次只结算一次；无敌帧生效；敌人死亡掉落正确。

## 8. 风险与开放问题

- 判定窗口与动画帧同步是手感关键，需要可视化调试（M27）。
- 近战与远程并存还是二选一，影响武器与敌人设计。
- 击退与碰撞/地形的交互可能造成穿墙，需要钳制。
- 状态效果的叠加与驱散规则需要产品定义。
