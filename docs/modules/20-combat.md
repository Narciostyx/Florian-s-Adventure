# M20 Combat

| 项 | 值 |
|---|---|
| 层 | L3 |
| 优先级 | P1 |
| 状态 | 已实现 |
| 依赖 | M01、M11、M13、M16 |
| 被依赖 | M21、M24、M25、M27 |

> 实测与踩坑：[构建记录](../build/m20-combat.md)

## 1. 目的

实现实时动作战斗的**判定与状态**：攻击窗口、伤害、受击/无敌、死亡、投射物与状态效果。表现层（动画、伤害数字、特效）由 app/M10/M06 消费事件完成。

## 2. 范围

**已实现**

- `Damage`：物理/火/冰/毒标签、来源、击退。
- `applyDamage`：读取 `player::Health`，尊重 M16 的闪避无敌窗口，击退经 `PhysicsWorld::move`。
- `AttackHitbox`：相对 owner 的偏移/尺寸、`activeFrom/activeTo` 时间窗、目标位掩码、伤害、击退；同一攻击实例对同一目标只结算一次。
- `Projectile`：真正的实体（`Transform` + `Projectile`），用 `PhysicsWorld::raycast` 检测实体/瓦片，用 `move` 推进；命中或撞墙后销毁。
- `StatusEffect`：Poison/Burn 持续伤害、Slow 移速倍率；刷新同类效果，到期移除。
- 死亡：`Health` 归零时记录 `DeathEvent`，携带可选 `LootSource::lootTable` 字符串；不直接销毁实体、不掷掉落。
- 事件缓冲：`DamageEvent`、`DeathEvent`、`ProjectileEvent`、`StatusEvent`，由组装根决定是否发布到 M03。
- `CombatStats`：攻击/命中/伤害/击杀/投射物/状态 tick。
- app 演示：两个敌人 dummy、两次近战、一支投射物、一次中毒；近战击杀一个 dummy。
- play 模式：右键在朝向前方生成 20×20 近战盒；伤害读取装备武器的
  `ItemDef::damage`（木剑 12，空手 5）；app 消费 `DeathEvent`、按 `LootSource`
  掷掉落、销毁敌人，并在玩家靠近时把 `LootDrop` 收进背包。

**未实现 / 后续**

- 连击、蓄力、扇形/圆形判定；v1 只有矩形 hitbox。
- 伤害抗性/暴击/元素反应表。
- 状态叠加规则（目前同类刷新）、驱散、免疫。
- 掉落结算/入包（M15/M18 归组装根）；投射物/特效资源（M05/M06）。
- 敌人决策（M21）、玩家输入（M16）、死亡动画（M10）。
- 攻击方向由 hitbox 偏移表达，没有自动朝向扇形。

## 3. 核心概念与数据结构

```cpp
enum class DamageType { Physical, Fire, Ice, Poison, Count };

struct Damage {
    int amount;
    DamageType type;
    scene::EntityId source;
    core::Vec2 knockback;
};

struct AttackHitbox {
    core::Vec2 offset;      // 相对 owner Transform
    core::Vec2 size;
    float activeFrom, activeTo;
    int damage;
    DamageType type;
    physics::CollisionMask targetMask;
    core::Vec2 knockback;
    std::uint32_t attackId;
};

struct Projectile {
    core::Vec2 velocity;
    float lifetime;
    int damage;
    DamageType type;
    physics::CollisionMask targetMask;
    scene::EntityId owner;
};

struct StatusEffect {
    StatusKind kind;        // Poison / Slow / Burn
    float duration;
    float magnitude;        // DoT: dps; Slow: 0.5 = half speed
    scene::EntityId source;
    float accumulated;      // internal DoT accumulator
};

struct LootSource { std::string lootTable; };
```

- 攻击窗口按 `elapsed` 推进；跨过窗口的 update 也会命中一次。
- 无敌判定复用 `player::PlayerController::isInvulnerable`，不在 M20 重算窗口。
- 击退调用 `PhysicsWorld::move`，M13 仍是唯一位置写入方。
- 死亡只记录事件；实体是否销毁、掉落是否入包由组装根决定。

## 4. 公共接口

```cpp
namespace fa::combat {

class ICombatService {
public:
    virtual bool applyDamage(scene::World&, scene::EntityId target, const Damage&) = 0;
    virtual void spawnAttack(scene::World&, scene::EntityId owner, const AttackHitbox&) = 0;
    virtual scene::EntityId spawnProjectile(scene::World&, const ProjectileDesc&) = 0;
    virtual bool applyStatus(scene::World&, scene::EntityId target, const StatusEffect&) = 0;
    virtual void update(scene::World&, float deltaSeconds) = 0;
};

class CombatService final : public ICombatService {
public:
    explicit CombatService(physics::PhysicsWorld& physics, player::PlayerTuning tuning = {});
    std::span<const DamageEvent> damageEvents() const;
    std::span<const DeathEvent> deathEvents() const;
    std::span<const ProjectileEvent> projectileEvents() const;
    std::span<const StatusEvent> statusEvents() const;
    void clearEvents();
    const CombatStats& stats() const;
    float slowFactor(const scene::World&, scene::EntityId) const;
};

}  // namespace fa::combat
```

## 5. 依赖关系

- 依赖 M01（数学/`Result`）、M11（`World`/`EntityId`/`Transform`）、M13（`Collider`/`PhysicsWorld`）、
  M16（`player::Health`/`PlayerTuning`/`PlayerController::isInvulnerable`）。
- **不依赖 M15/M18**：掉落表用字符串 id 事件交给组装根，伤害/状态只读写组件。
- **不依赖 M03/M06/M09/M10**：事件按调用记录，表现由 app 消费；没有 GL/UI/动画依赖。
- `fa_combat` 链接 `fa_core`、`fa_scene`、`fa_physics`、`fa_player`；没有 JSON/chrono/unordered_map，
  不需要 `/EHsc`。

## 6. 扩展点

- 新增攻击形状：把 `AttackHitbox` 扩展为 variant，查询改用 `overlapCircle`/扇形。
- 新增伤害类型/抗性：在 `applyDamage` 前插修正器，事件保留原始类型。
- 新增状态：扩展 `StatusKind`/更新逻辑；叠加策略按 kind 定制。
- 新增投射物行为：在 `Projectile` 加穿透/追踪字段，`updateProjectiles` 消费。
- 新增掉落：组装根读 `DeathEvent.lootTable`，用 M15/M18 掷表并入包。
- 新增伤害数字/特效：app 读 `DamageEvent` 生成 M06 绘制项或 M10 特效实体。

## 7. 验收

- `fa_tests.exe "[combat]"` → 7 用例 / 50 断言全绿。
- 全量 → 543 用例 / 30281 断言全绿。
- 三配置 `fa_app.exe --frames 60` 输出
  `m20: 3 attack(s), 5 hit(s), 51 damage, 1 kill(s), 1 projectile(s)/1 hit(s)/0 wall hit(s), 1 status tick(s), 10 event(s)`。
- `fa_app.exe --play-test` → 2 次攻击击杀 dummy，掉落 2 个种子并自动拾取。
- Debug/Development/Release 构建零 warning。

## 8. 风险与开放问题

- 攻击窗口与动画帧同步仍由 app 手动触发；没有 M10 事件自动接线。
- 矩形 hitbox 不跟朝向旋转；需要扇形/朝向偏移时再扩展。
- 击退直接 `move`，可能被墙夹住；没有速度衰减/受击硬直。
- 状态同类刷新，不叠加；DoT 按整数 tick，低 dps 可能延迟。
- M20 本身不销毁实体、不掷掉落；play 模式由 app 消费 `DeathEvent` 完成，并把
  `LootDrop` 写入 `world.loot`，读档时重建；脚本冒烟仍保留 dummy。
- 没有抗性/暴击/连击/蓄力。
- 投射物每步 `raycast` + `move`；数量大时需要批量查询/对象池。
- 事件缓冲每帧由调用方清空；忘记清空会持续增长。
