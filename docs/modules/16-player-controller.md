# M16 Player Controller

| 项 | 值 |
|---|---|
| 层 | L3 |
| 优先级 | P1 |
| 状态 | 设计完成 |
| 依赖 | M00–M15 |
| 被依赖 | M17、M18、M20、M25、M27 |

## 1. 目的

把输入转换为玩家角色的移动、朝向、工具使用、攻击与状态切换，是玩法输入的唯一汇聚点。

## 2. 范围

**负责**

- 移动：8 向或 4 向移动、加速度/减速度、速度上限、碰撞求解调用。
- 朝向：`Facing`（down/up/left/right），决定交互与攻击方向。
- 状态机：`Idle`、`Move`、`UseTool`、`Attack`、`Hurt`、`Dead`、`Menu`。
- 动作路由：使用工具（锄头/水壶/种子）、攻击、交互、打开背包。
- 耐力：消耗/恢复/耗尽惩罚（若产品确认）。
- 相机跟随：目标点、平滑、边界限制。
- 动画参数：速度、朝向、状态 → M10。
- 玩家持久数据：生命、耐力、金钱、位置、当前地图。

**不负责**

- 具体工具效果（M17/M19）。
- 伤害计算（M20）。
- 背包数据（M18）。
- 输入映射（M07）。

## 3. 核心概念与数据结构

```cpp
enum class PlayerState { Idle, Move, UseTool, Attack, Hurt, Dead, Menu };
enum class Facing { Down, Up, Left, Right };

struct PlayerTag {};
struct Velocity { glm::vec2 value{0.0f}; };
struct FacingDir { Facing value = Facing::Down; };
struct PlayerStateComp { PlayerState state = PlayerState::Idle; float stateTime = 0.0f; };
struct Health { int current = 100; int max = 100; };
struct Stamina { float current = 100.0f; float max = 100.0f; };
struct Wallet { int gold = 0; };
```

- 玩家实体由 `PlayerTag` 标记；系统只处理带该标签的实体。
- 状态切换有最短持续时间，避免动画抖动。
- 移动在 `UseTool`/`Attack` 期间可被限制（产品规则），由状态机决定。

### 移动与动作参数（ADR-0002 / ADR-0003）

- 输入来源：键鼠（`ActionMap` 映射）；不读 SDL 直接状态。
- 走 90 px/s，跑 150 px/s（`Shift` 切换），斜向归一化。
- 闪避：`Dodge` 动作 → 48px / 0.25s 位移曲线，0.05–0.20s 无敌，0.5s 冷却，消耗 25 耐力。
- 攻击：`LightAttack` 15 耐力、`HeavyAttack` 25 耐力，由 M20 校验与扣除。
- 耐力不足时动作被拒绝并触发一次反馈事件（不排队）。
- 精灵 16×24，`Transform::position` 为**精灵左上角**；脚部碰撞盒 10×6，`Collider::offset` 为 `{3, 18}`（见 [ADR-0003](../adr/0003-tile-and-collision-metrics.md)）。

## 4. 公共接口草案

```cpp
namespace fa::player {

/// M13 已实现：`PhysicsWorld` 是具体类，不存 `World&`，每次调用显式传世
/// 界。控制器只写 `Velocity`，位置由 `PhysicsSystem` 写。
class PlayerSystem : public scene::ISystem {
public:
    PlayerSystem(input::IInput& input, physics::PhysicsWorld& physics, anim::IAnimationService& anim);
    std::string_view name() const noexcept override { return "PlayerSystem"; }
    void onFixedUpdate(scene::World& world, const time::FrameContext& time,
                       scene::CommandBuffer& commands) override;
    void onVariableUpdate(scene::World& world, const time::FrameContext& time,
                          scene::CommandBuffer& commands) override;
};

struct PlayerIntent {
    glm::vec2 move;
    bool useTool;
    bool attack;
    bool interact;
    bool openInventory;
};

} // namespace fa::player
```

## 5. 依赖关系

- 依赖 M07（输入）、M10（动画）、M11（ECS）、M13（物理）、M15（物品/工具定义）。
- 不直接依赖 M17/M18/M19/M20；通过事件（`UseToolRequested`、`AttackRequested`、`InteractRequested`）解耦。
- 相机跟随使用 M06 的 `Camera2D`，不持有渲染器。

> **M13 已实现**（见 [M13 物理与碰撞](13-physics-collision.md)）。本模块落地时应当：
> 1. 玩家实体挂 `fa::physics::Collider`，直接复用 `fa::physics::kPlayerFeetCollider`
>    （10×6，offset `{3, 18}`，相对精灵左上角）；
> 2. `PlayerSystem` 只写 `fa::physics::Velocity::linear`（走 90 / 跑 150，斜向归一化），
>    **不要**自己加 `Transform::position`——位置由 `PhysicsSystem` 写；
> 3. 闪避用 `PhysicsWorld::move(world, player, DodgeDirection * 48.0f)` 一帧推完，
>    或在其上做 0.25s 的速度曲线；无敌窗口是 M20 的状态，不是物理的；
> 4. `PhysicsSystem` 必须排在 `PlayerSystem` 之后，否则动作晚一帧生效。

## 6. 扩展点

- 新增状态（游泳、攀爬、骑乘）在状态机中加入。
- 新增移动能力（冲刺、翻滚）作为状态 + 耐力消耗。
- 新增角色（多人/换装）通过组件参数化，不新增系统。
- 手柄与键鼠共用 `PlayerIntent`，输入层差异被隔离。

## 7. 实现阶段

P1，Phase B/C。先实现移动、朝向、动画、相机；再实现工具/攻击状态与耐力。验收：8 向移动流畅、不穿墙、朝向正确、相机不抖。

## 8. 风险与开放问题

- 8 向移动与 4 向动画的匹配需要美术规范；斜向时选择主导轴。
- 状态机与事件顺序：同一帧内“使用工具”和“攻击”冲突时优先级需明确。
- 耐力/翻滚/格挡是否进入 v1 未定；接口已预留。
- 相机平滑在像素对齐下会产生抖动；需在渲染阶段做像素对齐。
