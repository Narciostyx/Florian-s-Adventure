# M25 Game Flow

| 项 | 值 |
|---|---|
| 层 | L3 |
| 优先级 | P1 |
| 状态 | 设计完成 |
| 依赖 | M00–M24 |
| 被依赖 | M26、M27 |

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
- `Dialogue` 是 `Playing` 的子状态（玩法暂停、UI 活跃），也可用输入上下文实现。
- 所有场景切换在帧末执行，避免在系统更新中销毁当前世界。

## 4. 公共接口草案

```cpp
namespace fa::flow {

class IGameFlow {
public:
    virtual ~IGameFlow() = default;
    virtual Result<void> startNewGame(std::string_view slot) = 0;
    virtual Result<void> loadGame(std::string_view slot) = 0;
    virtual Result<void> saveGame() = 0;
    virtual void requestPause(bool paused) = 0;
    virtual void endDay() = 0;
    virtual void requestQuit() = 0;
    virtual AppState state() const = 0;
};

} // namespace fa::flow
```

## 5. 依赖关系

- 依赖 M04（时间暂停）、M09（菜单）、M11（场景栈）、M14（存档）、M22（日终）、M23（任务重建）、M24（结算）。
- 它是 composition root 之外的第二个“编排者”；只调用接口，不实现玩法。
- 不依赖 M26/M27；调试工具通过命令调用 `IGameFlow`。

## 6. 扩展点

- 新增流程状态（过场、教程、多人大厅）在状态机中加入。
- 新增结算步骤（出货箱、任务、成就）作为 `IEndOfDayStep` 链。
- 新增存档策略（退出自动保存、云同步）。
- 新增开场/过场播放器。

## 7. 实现阶段

P1，Phase E。先实现主菜单→新游戏→暂停→保存→退出；再实现日终结算、死亡、读档。验收：完整循环可跑通；读档后状态一致；日终自动存档不丢数据。

## 8. 风险与开放问题

- 全局状态机容易膨胀为“上帝对象”；严格限制为编排，不写规则。
- 日终结算涉及多个模块，失败时需要回滚或至少保证存档一致性。
- 暂停与输入上下文的重复控制需要统一。
- 死亡惩罚与复活规则为产品决策。
