# M25 Game Flow

| 项 | 值 |
|---|---|
| 层 | L3 |
| 优先级 | P1 |
| 状态 | 已实现 |
| 依赖 | M00–M24（实际链接 `fa_core`、`fa_save`、`fa_world`、`fa_economy`、`fa_narrative`） |
| 被依赖 | M26、M27 |

> 实测与踩坑：[构建记录](../build/m25-game-flow.md)

## 1. 目的

编排游戏高层流程：启动、主菜单、新游戏/读档、游玩、暂停、睡眠结算、死亡、退出。它是场景切换与全局状态转换的唯一入口。

## 2. 范围

**负责**

- 应用状态机：`Boot`、`MainMenu`、`Loading`、`Playing`、`Paused`、`Dialogue`、`GameOver`、`Quit`。
- 新游戏：世界初始化、玩家出生、初始物品、开场事件。
- 读档/存档：调用 M14，处理失败与版本不兼容提示。
- 睡眠/日终结算：暂停玩法、推进日期（M22）、触发 `DayChanged`、自动存档、恢复。
- 死亡：玩家死亡界面、复活惩罚、读档选项。
- 暂停菜单：继续、设置、保存、返回主菜单。
- 设置：音量、按键、显示模式、语言（预留）。
- 自动存档策略与槽位选择。
- 应用生命周期：窗口关闭请求、崩溃恢复提示。

**不负责**

- 具体玩法逻辑（各 L3 模块）。
- 存档格式（M14）。
- UI 控件实现（M09）。
- 场景内部实体管理（M11）。

### 存档流程（ADR-0002 决策 3）

- 主菜单提供「继续（自动槽）」「读取（3 手动槽）」「新游戏」。
- 存档界面支持 3 手动槽的保存/读取/覆盖确认；自动槽只读。
- 自动存档由 M22 时间事件触发，经本模块调度 M14 写入。
- 进入战斗/过场时挂起自动存档，回到安全状态后补一次。
- 退出游戏前若距上次存档超过 12 游戏小时，提示保存。

## 3. 核心概念与数据结构

```cpp
enum class AppState {
    Boot, MainMenu, Loading, Playing, Paused, Dialogue, GameOver, Quit
};

struct GameSession {
    std::string slot;
    double playSeconds = 0.0;
    bool dirty = false;
    int autosaveIntervalDays = 1;
};
```

- 状态转换集中在一个函数中，便于审查与调试。
- `Dialogue` 是独立的 `AppState`（玩法/时钟暂停、UI 活跃）；v1 用
  `enterDialogue()`/`exitDialogue()` 切换，后续也可映射为输入上下文。
- 设计约束：场景切换应在帧末执行，避免在系统更新中销毁当前世界；
  v1 尚未提供切换回调。

## 4. 公共接口草案

```cpp
namespace fa::flow {

class IGameFlow {
public:
    virtual ~IGameFlow() = default;
    virtual core::Status startNewGame(std::string_view slot) = 0;
    virtual core::Status loadGame(std::string_view slot) = 0;
    virtual core::Status saveGame() = 0;
    virtual void requestPause(bool paused) = 0;
    virtual core::Status endDay() = 0;
    virtual void requestQuit() = 0;
    [[nodiscard]] virtual AppState state() const noexcept = 0;
    [[nodiscard]] virtual const GameSession& session() const noexcept = 0;
};

} // namespace fa::flow
```

`endDay()` 需要把 M14/M22/M23/M24 的失败报告给调用方，因此返回 `core::Status`
而不是设计草案里的 `void`；`session()` 让菜单和调试面板读取槽位与脏标记。

## 5. 依赖关系

- 设计依赖 M04（时间暂停）、M09（菜单）、M11（场景栈）、M14（存档）、M22（日终）、
  M23（任务重建）、M24（结算）；v1 实际链接 `fa_core`、`fa_save`、`fa_world`、
  `fa_economy`、`fa_narrative`。
- 它是 composition root 之外的第二个“编排者”；只调用接口，不实现玩法。
  菜单/输入上下文、世界重建、存档应用通过回调交给组装根，`fa_flow` 不直接
  include 这些模块的实现。
- 不依赖 M26/M27；调试工具通过命令调用 `IGameFlow`。

## 6. 扩展点

- 新增流程状态（过场、教程、多人大厅）在状态机中加入。
- 新增结算步骤（作物生长、出货箱、任务、成就）先挂到 `EndOfDayHandler`；
  需要多个独立步骤时再引入 `IEndOfDayStep` 链。
- 新增存档策略（退出自动保存、云同步）。
- 新增开场/过场播放器。

## 7. 实现阶段

P1，Phase E。先实现主菜单→新游戏→暂停→保存→退出；再实现日终结算、死亡、读档。验收：完整循环可跑通；读档后状态一致；日终自动存档不丢数据。

**v1 状态：已完成。** 状态机、新游戏/读档、暂停/对话、日终结算、死亡/复活、自动存档与退出均落地；app smoke 按“新游戏 → 暂停 → 保存 → 日终 → 死亡 → 复活 → 主菜单 → 读档 → 退出”跑通。主菜单/暂停/存档槽界面已由 M09 接线（鼠标操作）。

## 8. 风险与开放问题

- 全局状态机容易膨胀为“上帝对象”；严格限制为编排，不写规则。
- 日终结算涉及多个模块，失败时需要回滚或至少保证存档一致性。
- 暂停与输入上下文的重复控制需要统一。
- 死亡惩罚与复活规则为产品决策。

**v1 落地结论**：`GameFlow` 只持有 M14/M22/M23/M24 的公开接口指针，世界重建、
存档内容构建与存档应用通过 composition root 回调完成，因此没有把玩法规则吸进
流程层。日终结算同步执行：推进时钟 → `EndOfDayHandler`（M19 生长）→ M24 结算 →
M23 重置每日上限 → 自动存档；
若自动存档失败，日期推进已发生且不回滚，调用方会收到错误状态。

## 9. 实际落地（v1）

- `fa/flow/FlowTypes.hpp`：`AppState`、`GameSession`、`FlowEvent`/`FlowStats`、
  7400 错误段、`autosave`/`slot1..3` 槽位约定。
- `fa/flow/Sleep.hpp`：`sleepHoursUntil()`、`sleepRecovery()` 与睡眠常量。
- `fa/flow/GameFlow.hpp` + `src/GameFlow.cpp`：`IGameFlow` 与 `GameFlow`；
  状态转换集中在 `transition()`，非法转换返回 `flow_error::kInvalidTransition`。
- 回调：`NewGameHandler`（重建世界/初始物品）、`SaveBuilder`（填 `SaveContext`）、
  `LoadHandler`（应用 `SaveFile`）、`PauseHandler`（暂停 UI/输入上下文）、
  `EndOfDayHandler`（时钟推进后、经济/叙事/自动存档前运行，供 M19 等按天逻辑挂接）。
- 日终：`endDay()` 调用 `WorldCalendar::skipToNextDay()`、`EndOfDayHandler`、
  `EconomyService::onDayChanged()`、`NarrativeService::onDayChanged()`，再按
  `autosaveIntervalDays` 写 `autosave`；`onDayChanged()` 供外部日变化（如自然跨天）
  复用同一结算路径。
- 睡眠规则：`fa/flow/Sleep.hpp` 提供 `sleepHoursUntil()` 与 `sleepRecovery()`。
  任意时间可睡，醒来为次日 `wakeHour`；满 `fullHours` 恢复全部缺失体力，
  不足时按 `(小时/满时长)²` 衰减，最低 `kMinSleepRecovery`（1%）。
  默认值是 `kWakeHour=8`、`kFullSleepHours=8`；app 从 `config/game.json` 传入
  `gameplay.wake_hour`/`full_sleep_hours`。`setDayStartHour()` 在自动存档前生效，
  早于或晚于默认 08:00 都会正确恢复。
  睡眠本身不消耗体力，也不存在强制晕倒时刻。
- 存档：`saveGame()` 写当前会话槽；`saveToSlot(slot)` 供菜单显式写 `slot1..3`，
  不改变会话槽。
- 暂停：`Playing` 以外的状态统一暂停 M22 时钟；`Dialogue`/`GameOver`/`Paused`
  之间不会误开玩法。
- 菜单：composition root 用 M09 绘制主菜单（新游戏/继续/存档槽/退出）与暂停面板
  （继续/保存/存档槽/主菜单），`Esc` 切换暂停；菜单只调用本模块的公开状态接口。
- 已知缺口：app 已把日期/时间/天气、玩家位置/朝向/耐力、背包/快捷栏/装备、农田
  与经济接入 `SaveBuilder`/`LoadHandler`，但叙事、AI、世界对象仍未持久化；
  同图农场/林地传送已由 app 消费 `portalAt` 完成；`SceneStack` 独立场景替换、
  输入上下文仍待接线。死亡路径已接：生命归零 → `requestGameOver`/`respawn` →
  床边复活 + 10% 金币；死亡菜单/读档选项 UI 仍缺。
