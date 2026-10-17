# M23 NPC / Dialogue / Quests

| 项 | 值 |
|---|---|
| 层 | L3 |
| 优先级 | P2 |
| 状态 | 设计完成 |
| 依赖 | M00–M22 |
| 被依赖 | M24、M25、M26 |

## 1. 目的

提供 NPC 数据、对话图、好感度、任务与日志系统，支撑叙事与目标驱动，同时保持与玩法系统的低耦合。

## 2. 范围

**负责**

- NPC 定义：ID、名字、立绘、日程、喜好、商店关联。
- 好感度： hearts、每日对话上限、送礼加成、生日加成。
- 对话图：节点（文本、选项、条件、跳转、动作）；条件表达式（好感、任务状态、日期、物品）。
- 对话动作：给物品、加好感、开始任务、打开商店、播放动画。
- 任务：目标类型（收集、送达、击杀、种植、对话）、进度、完成条件、奖励。
- 日志/任务追踪 UI 数据。
- 事件：`DialogueStarted`、`DialogueEnded`、`QuestCompleted`。

**不负责**

- 对话 UI 渲染（M09）。
- 物品/金钱实际变更（M18/M24，通过动作接口）。
- NPC 移动（M21）。
- 过场动画编辑器（M26 可扩展）。

## 3. 核心概念与数据结构

```cpp
struct DialogueNode {
    std::string id;
    std::string speaker;
    std::string text;
    std::vector<DialogueChoice> choices;
    std::string next;
    std::vector<DialogueAction> actions;
    std::string condition; // 表达式，可空
};

struct QuestDef {
    ContentId id;
    std::string title;
    std::string description;
    std::vector<QuestObjective> objectives;
    std::vector<ItemStack> rewards;
    int goldReward = 0;
};

struct QuestState {
    ContentId id;
    bool active = false;
    bool completed = false;
    std::vector<int> progress;
};
```

- 对话图是只读数据；运行状态（当前节点、已看过的对话）单独保存。
- 条件表达式使用受限语法（比较、与或非、变量查询），不做通用脚本。
- 任务进度通过事件更新（`ItemAdded`、`EntityDied` 等），QuestSystem 订阅。

## 4. 公共接口草案

```cpp
namespace fa::narrative {

class IDialogueService {
public:
    virtual ~IDialogueService() = default;
    virtual Result<void> start(ContentId dialogue, scene::EntityId speaker) = 0;
    virtual void advance() = 0;
    virtual void choose(int index) = 0;
    virtual bool active() const = 0;
};

class IQuestService {
public:
    virtual ~IQuestService() = default;
    virtual void startQuest(ContentId id) = 0;
    virtual void reportProgress(QuestEvent event) = 0;
    virtual std::vector<QuestState> activeQuests() const = 0;
};

} // namespace fa::narrative
```

## 5. 依赖关系

- 依赖 M03（事件）、M09（UI 调用）、M11（NPC 实体）、M15（NPC/对话/任务定义）、M18（物品）、M24（金钱/商店）。
- 不依赖 M19/M20；任务目标通过事件订阅，不直接查询玩法内部状态。
- M25 负责开始新游戏/读档时重建任务状态。

## 6. 扩展点

- 新增条件函数与动作类型，注册到表达式/动作表。
- 新增任务目标类型（探索区域、拍照、钓鱼）。
- 新增对话表现（立绘表情、语音、逐字速度）。
- 节日/事件脚本作为对话图的组合。

### 内容驱动扩展（ADR-0002）

- 对话、任务、日程全部 JSON 化，季节以 `seasonMask` 过滤可用对话/任务。
- 新增 NPC 不需要改代码，只需内容行 + 日程表。
- 温室相关台词通过 `AreaKind` 条件触发，不硬编码坐标。
- 任务奖励走 M18/M24 的通用接口，不直接改玩家状态。

## 7. 实现阶段

P2，Phase E。先实现线性对话与送礼好感；再实现对话图、任务、日志。验收：对话选项按条件显示；送礼提升好感；收集任务完成自动结算奖励。

## 8. 风险与开放问题

- 条件表达式若不受限会演变为脚本引擎；坚持白名单操作。
- 任务进度的事件来源需要统一，否则各系统各自实现。
- 对话与 UI 的输入上下文切换需要防重入。
- v1 是否需要任务系统？若不需要，可先只做对话。
