# M16 Player Controller

| 项 | 值 |
|---|---|
| 层 | L3 |
| 优先级 | P1 |
| 状态 | 已实现 |
| 依赖 | M07、M11、M13；M10/M20 已实现，M15 仍由组装根接线 |
| 被依赖 | M17、M18、M20、M25、M27 |

## 1. 目的

把输入转换为玩家角色的移动、朝向、工具使用、攻击与状态切换，是玩法输入的唯一汇聚点。
本模块只写 `physics::Velocity`，位置仍由 M13 的 `PhysicsSystem` 写；具体工具效果、
伤害、背包与动画分别由 M17/M19、M20、M18、M10 负责。

## 2. 范围

**已实现**

- 移动：8 向、斜向归一化、走 90 / 跑 150、加速度 900 / 减速度 1200。
- 朝向：`Facing` 八向（四个正方向 + 四个斜向），按 45° 扇区选择；静止时保持上一次朝向。
  `isDiagonal()`/`cardinalFacing()` 供只需要四向的消费者折叠斜向。
- 状态机：`Idle`、`Move`、`UseTool`、`Attack`、`Dead`；`Hurt`/`Menu` 保留。
- 动作路由：`UseTool`、`Attack`、`Interact`、`OpenInventory`、`Dodge`，
  以及 `ActionRejected`；记录为纯数据，由组装根决定转发给 M17/M18/M19/M20。
- 动作锁：工具/攻击持续 0.2 s，期间移动减速到零；`Interact` 不锁移动。
- 耐力：默认 100；闪避消耗 25；不闪避时每秒恢复 20。
- 闪避：48 px / 0.25 s，冷却 0.5 s；0.05–0.20 s 为无敌窗口（M20 读取，M16 不结算伤害）。
- 输入适配：`PlayerSystem` 从 M07 的 `IInput` 生成 `PlayerIntent`，并缓存动作 id。
- 动画参数：`PlayerAnimationParams`（速度/朝向/状态/速度值/是否移动）供 M10 消费。
- 冒烟：脚本化移动轴 + 固定动作绑定；输出状态、朝向、速度、耐力、更新/移动/闪避计数。

**后续接入（本模块不负责）**

- 具体工具效果：M17/M19 的 `IToolHandler`。
- 伤害/受击/死亡规则：M20；M16 只在 `Health <= 0` 时进入 `Dead`。
- 背包扣除/切换：M18。
- 内容 ID：M15 已实现；`PlayerIntent::payload` 仍是不透明整数，接线时替换。
- 动画播放：M10；M16 只产出参数。
- 相机跟随：已由 M06 `Camera2D::follow()` 实现，app 以玩家精灵中心为目标。
- 菜单/UI 状态：M09/M25；`Menu` 枚举保留但未被控制器主动进入。

## 3. 核心概念与数据结构

```cpp
enum class Facing : std::uint8_t {
    North, East, South, West, NorthEast, SouthEast, SouthWest, NorthWest, Count
};
enum class PlayerState : std::uint8_t { Idle, Move, UseTool, Attack, Hurt, Dead, Menu, Count };
enum class PlayerActionKind : std::uint8_t {
    UseTool, Attack, Interact, OpenInventory, Dodge, ActionRejected, Count
};

struct PlayerTag {};
struct FacingDir { Facing value = Facing::South; };
struct PlayerStateComp { PlayerState state; float stateTime; float actionLock; };
struct Health { int current = 100; int max = 100; };
struct Stamina { float current = 100.0f; float max = 100.0f; };
struct Wallet { int gold = 0; };
struct DodgeState { float timeLeft; float cooldown; Facing direction; };

struct PlayerIntent {
    core::Vec2 move;
    bool run, useTool, attack, interact, dodge, openInventory; // 动作是按下边沿
    std::uint32_t payload;
};

struct PlayerActionRequest {
    PlayerActionKind kind;
    scene::EntityId player;
    Facing facing;
    std::uint32_t payload;
    int errorCode;
};
```

- `PlayerTag` 标记唯一由 `PlayerSystem` 驱动的实体；可以有多个，但 v1 只应有一个。
- `PlayerStateComp::actionLock` 是工具/攻击的移动锁计时；`stateTime` 只用于调试/动画。
- `DodgeState` 是 M20 的无敌窗口来源；`isInvulnerable()` 只读，不改伤害。
- `PlayerIntent` 的动作布尔是**边沿**（`wasPressed`），`run` 是持续按住。
- `payload` 与 M17 一致；M15 已实现，接线后统一换成 `ContentId`。

## 4. 公共接口

```cpp
class PlayerController {
public:
    explicit PlayerController(PlayerTuning tuning = {});
    static void attach(World&, EntityId, Stamina = {}, Facing = Facing::South);

    void update(World&, EntityId, const PlayerIntent&, float dt);

    static bool isDodging(const World&, EntityId) noexcept;
    static bool isInvulnerable(const World&, EntityId, const PlayerTuning&) noexcept;

    std::span<const PlayerActionRequest> events() const noexcept;
    const PlayerStats& stats() const noexcept;
    const PlayerAnimationParams& animationParams() const noexcept;
};

class PlayerSystem final : public scene::ISystem {
public:
    explicit PlayerSystem(input::IInput& input, PlayerTuning tuning = {});
    void onFixedUpdate(World&, const FrameContext&, CommandBuffer&) override;
    void setScriptedMove(core::Vec2); // 冒烟用
    PlayerController& controller() noexcept;
};
```

- `attach()` 只添加缺失组件，不覆盖已存在的耐力/朝向；实体必须已有 `Transform` 与 `Velocity`。
- `PlayerSystem` 必须注册在 `PhysicsSystem` **之前**，否则速度晚一帧生效。
- 动作 id 名字在 `PlayerTypes.hpp`：`Run`、`Dodge`、`UseTool`、`Attack`、`Interact`、`Inventory`；
  绑定由 composition root 完成（M07 不包含玩法键位）。

### 移动与动作规则

1. 输入向量归一化；走/跑目标速度为 90/150，斜向不会更快。
2. 速度用加速度/减速度逼近目标；不是瞬移。
3. 朝向按 45° 扇区更新为八向；边界在距正方向 22.5°，零向量保持不变。
4. `UseTool` 优先于 `Attack`；二者都会设置 0.2 s 动作锁并让移动减速到零。
5. `Interact`/`OpenInventory` 只记录请求，不锁移动。
6. 闪避期间速度固定为 `dodgeDistance / dodgeDuration`；不能再次闪避/使用工具/攻击。
7. 死亡（`Health.current <= 0`）后速度清零、状态 `Dead`、忽略输入。
8. 耐力不足/冷却中/无方向/忙碌都记录 `ActionRejected`，不排队。

## 5. 依赖关系

- 直接引用 `fa_core`、`fa_input`、`fa_time`、`fa_scene`、`fa_physics`。
- **不引用 `fa_tilemap`、`fa_interaction`、`fa_content`、`fa_assets`**：
  - 交互/放置目标由 M17 解析；M16 只产出 `PlayerActionRequest`。
  - 工具具体效果由 M19/M20 的处理器负责。
  - 内容 ID 暂用 `payload`。
- `PlayerSystem.cpp` 因为 include `fa/input/Input.hpp`（间接 `<unordered_map>`）需要 `/EHsc`；
  控制器本身只用 core/scene/physics。
- 不写 `Transform::position`；只写 `Velocity`、朝向、状态、耐力、闪避计时。
- 不持有 `World&`；每次 `update` 显式传入。

## 6. 实现与测试

```text
src/engine/include/fa/player/   PlayerTypes, PlayerController, PlayerSystem, PlayerError
src/engine/player/src/          PlayerTypes.cpp, PlayerController.cpp, PlayerSystem.cpp
```

测试：`tests/player.test.cpp`，15 个用例 / 140 条断言，覆盖：

- 名称、八向朝向（含 22.5° 边界与斜向单位向量）；
- `attach` 只补缺失组件并保留已有耐力/朝向；
- 走/跑速度、加速度、减速度、斜向归一化；
- 朝向切换与静止保持；
- 闪避耐力消耗、速度、冷却、无敌窗口；
- 耐力不足/无方向/忙碌/冷却拒绝；
- 工具/攻击动作锁与 `UseTool > Attack` 优先级；
- 交互/背包请求不锁移动；
- 死亡忽略输入；
- 耐力恢复与上限；
- `PlayerSystem` 从 `IInput` 生成意图、脚本化移动覆盖输入轴。

冒烟：`DemoScene` 的标记现在由 `PlayerSystem` 驱动；Debug 实测 `move`、facing east、
速度 90、耐力 100、114 次更新、0 闪避、0 主动动作，标记仍被墙挡在 x=344。

## 7. 扩展点

- 新状态（游泳、攀爬、骑乘）加进 `PlayerState` 与状态机。
- 新移动能力（冲刺、翻滚）作为状态 + 耐力消耗。
- 新角色通过组件参数化（不同 `PlayerTuning`），不新增系统。
- 手柄/键鼠共用 `PlayerIntent`；输入差异留在 M07。
- `PlayerActionRequest` 可由组装根转成 M17 的 `interaction::InteractionRequest` 或 M03 事件。

## 8. 风险与开放问题

- 冒烟标记是 8×8，不是 16×24 角色；真实玩家应使用 `kPlayerFeetCollider`（10×6，offset `{3,18}`）。
- `UseTool`/`Attack` 的动作锁固定 0.2 s，未按工具/轻重攻击区分。
- 闪避期间完全禁止其他动作；是否允许取消攻击仍需手感原型。
- 无敌窗口只暴露给 M20，M16 不结算伤害；窗口边界是闭区间。
- 多玩家共享同一个 `PlayerIntent`（`PlayerSystem` 对所有 `PlayerTag` 应用同一意图）；v1 只应有一个。
- `Menu`/`Hurt` 状态保留但未由控制器主动进入；需要 M09/M20/M25 接入。
- 八向朝向已实现，但 M10 的动画 `Direction` 仍是四向；斜向动画需要组装根映射。
- 相机跟随由 M06 `Camera2D::follow()` 负责；控制器本身不感知相机。
- 内容 ID 仍是 `payload`；M15 已实现，接线时统一。
- 动作请求只记录不发布；M03 事件化由组装根决定。

