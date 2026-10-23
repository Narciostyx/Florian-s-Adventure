# M23 NPC / Dialogue / Quests —— 构建记录 / 实测

对应模块文档：[23-npc-dialogue-quests.md](../modules/23-npc-dialogue-quests.md)

> **状态：已实现并三配置实测通过。** v1 提供 NPC/对话图/好感度/任务运行时：
> 白名单条件、对话动作、每日送礼、事件驱动的任务进度与奖励；app 用一个 NPC
> 跑通“对话 → 接任务 → 收集 → 完成 → 送礼”，并留下一个可交互的对话面板。

## 1. 实际落地

新增工程 `src/engine/narrative/fa_narrative.vcxproj`（GUID `{C0DE0019-…0019}`），
引用 `fa_core`、`fa_content`、`fa_inventory`、`fa_scene`。`NarrativeDatabase.cpp`
使用 nlohmann/json，因此本工程打开 `/EHsc`。

| 文件 | 职责 |
|---|---|
| `fa/narrative/NarrativeTypes.hpp` | NPC/对话节点/选项/条件/动作/任务/事件/统计 |
| `fa/narrative/NarrativeDatabase.hpp` + `src/NarrativeDatabase.cpp` | JSON 加载、查找、重复/引用校验 |
| `fa/narrative/NarrativeService.hpp` + `src/NarrativeService.cpp` | 对话状态机、好感度、送礼、任务进度/奖励 |
| `tests/narrative.test.cpp` | `[narrative]` 10 用例 / 74 断言 |

### 关键实现选择

- **条件白名单，不是表达式引擎**：`DialogueCondition::Kind` 只包含
  `Always`/`AffinityAtLeast`/`QuestActive`/`QuestCompleted`/`HasItem`/`Flag`/`Season`。
  这样不会演化成脚本语言，存档格式也可预测。
- **动作白名单**：`GiveItem`/`AddAffinity`/`StartQuest`/`CompleteQuest`/`SetFlag`/`OpenShop`。
  商店只记录事件，实际交易留给 M24。
- **对话图**：节点有 `speaker`/`text`/`next`/`conditions`/`actions`/`choices`；
  `availableChoices()` 过滤条件后返回“显示索引”，`choose()` 再映射回真实选项。
  节点条件失败时沿 `next` 回退，没有 `next` 就结束。
- **好感度**：每天第一次对话 +1；送礼普通 +1、喜欢 +3，每天一次；
  `onDayChanged()` 重置每日上限。喜好来自 `NpcDef::likedItems`。
- **任务**：`Collect`/`Deliver`/`Kill`/`Plant`/`Talk` 五种目标；`reportEvent()`
  只推进匹配的活动任务，全部目标满足后自动完成。
- **奖励**：物品奖励通过 M18 `Inventory::add()`；金币只记录在
  `NarrativeEvent::QuestCompleted` 的 `amount` 里，M24 接钱包。
- **事件**：`DialogueStarted/NodeEntered/ChoiceSelected/DialogueEnded/GiftGiven/
  QuestStarted/QuestProgress/QuestCompleted/ActionApplied`，由 app 读取后清理。
- **app 集成**：
  - 内联 `kNarrativeText`：一个 `npc.elder`、一个分支对话、一个收集任务。
  - `DemoScene::runNarrativeDemo()` 跑通完整流程，并再次打开 `greet` 节点，
    让对话面板和数字键选择在可见运行时也能工作。
  - `buildHud()` 画底部对话框：说话人、正文、编号选项；`onEvents()` 把
    Digit1–3 映射到 `choose()`，Enter/Space 映射到 `advance()`，Esc 结束。
  - **play 集成**：农场 Elder 旁 `E` 调用 `startNpcDialogue()` 并进入 M25
    `Dialogue`；对话结束由 `syncDialogueState()` 退出，不会卡住。叙事背包指针
    指向玩家快捷栏；收获/拾取上报 `Collect`，`QuestCompleted` 由 app 加金币并
    显示 `QUEST COMPLETE`，HUD 增加任务行。
  - 摘要新增 `m23:` 行，smoke OK 加入 `+M23`。

### 内容格式

```json
{
  "schema_version": 1,
  "npcs": [{"id": "npc.elder", "name": "Elder", "liked_items": ["item.turnip"]}],
  "dialogues": [{"id": "dialogue.elder", "npc": "npc.elder", "start": "greet",
                 "nodes": [{"id": "greet", "text": "...", "choices": [...]}]}],
  "quests": [{"id": "quest.turnip", "objectives": [{"kind": "collect",
              "target": "item.turnip", "required": 1}], "rewards": [...]}]
}
```

条件/动作示例：

```json
{"kind": "affinity_at_least", "id": "npc.elder", "value": 1}
{"kind": "give_item", "id": "item.turnip_seed", "amount": 2}
```

## 2. 测试

`[narrative]` 覆盖：

- 数据库加载、查找、缺失 NPC/重复 id/缺失起点错误。
- 对话开场、当前节点、条件过滤后的选项数量。
- 选择动作、节点跳转、`advance()` 结束、事件与统计。
- 任务开始、匹配/不匹配事件、自动完成、物品奖励、重复开始/重复完成错误。
- 送礼消耗物品、喜欢加成、每日上限与 `onDayChanged()` 重置。
- 缺失礼物错误；clear 重置运行时状态。

实测：

```text
fa_tests.exe "[narrative]" → All tests passed (74 assertions in 10 test cases)
fa_tests.exe               → All tests passed (29701 assertions in 520 test cases)
fa_app.exe --frames 60     → m23: active, node greet, 2 choice(s), affinity 4,
                             quest completed, seed 4, 2 dialogue(s) [1 ended],
                             4 node(s), 1 choice(s), 1 gift(s), 12 event(s)
                           → M02+…+M22+M23+M26+M27 smoke test OK
```

## 3. 遇到的问题与结论

1. **M15 没有 NPC/对话/任务表。** v1 让 M23 自带 `NarrativeDatabase`，避免在
   M15 解析器里一次性扩三张表；代价是物品引用只做 `item.` 前缀校验，不查 M15。
   后续应把三张表迁入 M15 或让 M23 接收 M15 的引用校验器。
2. **条件不能做成自由表达式。** 用 typed enum 白名单，避免存档里出现无法静态
   校验的脚本；新增条件需要同时改枚举、解析器和 `evaluate()`。
3. **对话面板与编辑器抢数字键。** `onEvents()` 里先判断编辑器是否 active；
   编辑器打开时数字键归编辑器，否则归对话。
4. **本地变量名 `narrative` 隐藏了 SmokeSink 成员。** `/W4 /WX` 下 C4458 报错；
   `buildHud()` 的局部引用改名为 `dialogueState`。
5. **送礼事件的 `npc` 字段。** `record()` 默认写当前对话 NPC；送礼可能不在对话中，
   所以 `giveGift()` 在记录后显式补 `m_events.back().npc = npc`。
6. **任务完成与奖励不是事务。** 背包满时物品奖励可能丢失，但任务仍标记完成；
   M24/M18 需要提供原子奖励接口。
7. **对话暂停靠 M25 状态。** play 对话进入 `AppState::Dialogue`，`gameplayActive()`
   会屏蔽固定步与快捷栏；M07 还没有按 `AppState` 自动切输入上下文，后续应补
   上下文栈而不是继续散落 `gameplayActive()` 判断。

## 4. 验证命令

```powershell
$env:VCPKG_ROOT='D:\CPPLibraries\vcpkg'
$msbuild='C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'
foreach ($cfg in @('Debug','Development','Release')) {
  & $msbuild FloriansAdventure.sln /t:Rebuild /p:Configuration=$cfg /p:Platform=x64 /m /nologo /v:m
  & ".\build\bin\x64\$cfg\fa_tests.exe"
  & ".\build\bin\x64\$cfg\fa_app.exe" --frames 60
}
```

## 5. 已知限制 / 后续

- 没有 M15 集成：NPC/对话/任务定义在 M23 自己的数据库里，物品引用未跨表校验。
- 存档由 P0-3 接入：`NarrativeSnapshot` 保存好感/flags/每日上限/任务进度；
  对话节点历史与进行中的对话不保存。
- 没有 NPC 日程/移动：M21 不读取对话或任务状态；M23 也不驱动 NPC。
- 没有商店：`OpenShop` 只记录动作，M24 负责实际交易。
- 没有任务日志 UI、对话历史、立绘/表情/逐字/语音。
- 没有本地化；文本是单个 `std::string`。
- 没有条件表达式、任务链前置、失败/放弃任务、重复任务、每日任务。
- `Deliver` 不消耗物品；`Collect` 只按事件推进，不检查背包。
- 金币奖励已由 app 读取 `QuestCompleted` 后写入 `EconomyService`；M23 自身仍不持有钱包。
- 对话已通过 M25 `Dialogue` 暂停玩法并防重入；M07 输入上下文切换仍未实现。
