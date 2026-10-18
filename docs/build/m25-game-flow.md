# M25 Game Flow —— 构建记录 / 实测

对应模块文档：[25-game-flow.md](../modules/25-game-flow.md)

> **状态：已实现并三配置实测通过。** v1 提供应用状态机、新游戏/读档、暂停/对话、
> 日终结算、死亡/复活、自动存档与退出；日终顺序固定为“推进时钟 → M24 结算 →
> M23 每日重置 → 自动存档”。app 用一条 smoke 流程跑通
> “新游戏 → 暂停 → 保存 → 日终 → 死亡 → 复活 → 主菜单 → 读档 → 退出”。

## 1. 实际落地

新增工程 `src/engine/flow/fa_flow.vcxproj`（GUID `{C0DE001B-…001B}`），引用
`fa_core`、`fa_save`、`fa_world`、`fa_economy`、`fa_narrative`。工程不解析 JSON，
也没有 `<chrono>`/`<unordered_map>`，因此**不需要** `/EHsc`。

| 文件 | 职责 |
|---|---|
| `fa/flow/FlowTypes.hpp` | `AppState`、`GameSession`、`FlowEvent`/`FlowStats`、7400 错误段、槽位约定 |
| `fa/flow/GameFlow.hpp` + `src/GameFlow.cpp` | `IGameFlow`/`GameFlow` 状态机、保存/读档、暂停、日终、自动存档、退出 |
| `tests/flow.test.cpp` | `[flow]` 6 用例 / 118 断言 |

### 关键实现选择

- **状态转换集中**：`transition(next)` 统一检查允许的边并记录 `StateChanged`；
  非法转换返回 `flow_error::kInvalidTransition`，不会把状态卡在中间。
- **暂停由状态推导**：只有 `Playing` 解冻 M22 时钟；`Paused`、`Dialogue`、
  `GameOver`、`MainMenu`、`Loading`、`Quit` 都暂停。`PauseHandler` 只负责通知
  composition root 切换 UI/输入上下文。
- **流程层不碰玩法规则**：世界重建、`SaveContext` 构建、`SaveFile` 应用分别通过
  `NewGameHandler`、`SaveBuilder`、`LoadHandler` 回调完成。`GameFlow` 只持有
  M14/M22/M23/M24 的公开指针。
- **日终同步结算**：`endDay()` 依次调用 `WorldCalendar::skipToNextDay()`、
  `EconomyService::onDayChanged()`、`NarrativeService::onDayChanged()`，然后按
  `autosaveIntervalDays` 写自动槽。`onDayChanged()` 复用同一路径但不推进时钟，
  供 26:00 强制睡眠等外部日变化使用。
- **槽位**：手动槽 `slot1..slot3`，自动槽 `autosave`；槽名校验与 M14 一致
  （字母/数字/`_`/`-`，1..64）。`setAutosaveIntervalDays()` 最小为 1。
- **自动存档**：平时按天间隔触发；`autosaveOnQuit` 为真且会话脏时，退出前写
  `autosave` 槽。自动存档成功会清除 `dirty` 并重置间隔计数。
- **错误语义**：`startNewGame`/`loadGame`/`saveGame`/`endDay` 返回 `core::Status`；
  自动存档失败时日期推进已经发生，不回滚，只把失败状态交给调用方。
- **app 集成**：
  - `initializeFlow()` 用 `fillSaveContext()`/`applySaveFile()` 接 M14；
    `fillSaveContext()` 同时服务 M14 的 `autosave` smoke。
  - `runFlowDemo()` 在 M14/M22/M23/M24 的脚本之后运行，最后读回 `slot1`，
    因此 M22 摘要仍停在保存时的 `day 1 07:40`。
  - HUD 不新增菜单；摘要新增 `m25:` 行，smoke OK 加入 `+M25`（Release 也包含）。

### 状态转换表

| 从 \ 到 | MainMenu | Loading | Playing | Paused | Dialogue | GameOver | Quit |
|---|---|---|---|---|---|---|---|
| Boot | ✅ | ✅ | — | — | — | — | ✅ |
| MainMenu | — | ✅ | — | — | — | — | ✅ |
| Loading | ✅ | — | ✅ | — | — | — | ✅ |
| Playing | ✅ | — | — | ✅ | ✅ | ✅ | ✅ |
| Paused | ✅ | — | ✅ | — | — | ✅ | ✅ |
| Dialogue | ✅ | — | ✅ | — | — | ✅ | ✅ |
| GameOver | ✅ | ✅ | ✅ | — | — | — | ✅ |

`Playing → Playing` 等同一状态转换视为成功空操作，不记事件。

## 2. 测试

`[flow]` 覆盖：

- Boot → 新游戏 → 暂停/恢复 → 对话/结束 → 死亡/复活 → 主菜单 → 退出。
- 非法槽位、重复新游戏、退出后继续新游戏。
- 新游戏回调失败回到主菜单且不增加 `newGames`。
- 保存 builder 写上下文、保存成功后清 `dirty`、保存失败保留 `dirty`。
- 读档调用 `LoadHandler`、恢复槽位/playSeconds、读档失败与 apply 失败回到主菜单。
- 无保存服务、无会话、非法槽位、游戏中重复读档。
- 日终推进日期、M24 每日结算事件、按间隔自动存档、关闭自动存档、外部日变化。
- 退出自动存档与关闭 `autosaveOnQuit` 的对照。

实测：

```text
fa_tests.exe "[flow]" → All tests passed (118 assertions in 6 test cases)
fa_tests.exe          → All tests passed (29909 assertions in 536 test cases)
fa_app.exe --frames 60 → m25: quit, slot slot1, day 1, 1.50s, dirty no;
                         1 new / 1 load(s) (0 failed) / 1 save(s) / 2 autosave(s),
                         1 day end(s), 1 pause(s)/1 resume(s), 1 death(s)/1 respawn(s),
                         1 menu return(s), 1 quit(s), 10 transition(s), 20 event(s);
                         new ok, pause ok, save ok, day ok, over ok, respawn ok,
                         menu ok, load ok, quit ok
                       → M02+…+M25+M26+M27 smoke test OK
```

## 3. 遇到的问题与结论

1. **设计草案的 `endDay()` 是 `void`，但结算会失败。** 自动存档或 M24 结算失败需要
   让调用方看见，所以接口改为返回 `core::Status`；文档 §4 已同步。
2. **自动存档失败不能假装日终失败。** 日期已经推进，回滚会与 M22 的内部状态冲突；
   返回错误只表示“日终完成但自动存档失败”，`dirty` 保持为真。
3. **新游戏重置会话时不能丢掉自动存档间隔。** `startNewGame()` 先保存当前
   `autosaveIntervalDays`，再重建 `GameSession`；否则调用方刚设置的间隔会被重置。
4. **流程层必须只做编排。** 初版容易把场景重建、背包初始化和存档字段塞进
   `GameFlow`；最终全部放进回调，`fa_flow` 只依赖公开头，不 include 玩法实现。
5. **暂停必须由状态统一推导。** 手动在 `requestPause`、`enterDialogue`、
   `requestGameOver` 里各调一次日历容易漏；`transition()` 统一调用
   `applyPauseState()` 后，只有 `Playing` 解冻时钟。
6. **app 的 smoke 顺序。** `runFlowDemo()` 放在 M14/M22/M23/M24 脚本之后，
   并在最后读回 `slot1`；`applySaveFile()` 恢复日历，所以摘要里的 M22 仍是
   `day 1 07:40`，不会因为 M25 的日终测试漂到第二天。
7. **M25 不实现菜单 UI。** 状态机、槽位和回调已就绪，但主菜单/暂停菜单仍要
   M09 接线；app 目前只在摘要里演示流程。

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

本次实测：

- Debug / Development / Release 均零 warning（`/W4 /WX`）。
- `fa_tests.exe`：536 用例 / 29909 断言全绿。
- `fa_app.exe --frames 60`：三配置 exit 0，smoke OK 包含 M25。
- Release 不链接 M26/M27，但 M25 正常参与。

## 5. 已知限制 / 后续

- 没有主菜单、暂停菜单、存档槽选择界面；只有状态机与回调。
- `LoadHandler` 在 app 里只恢复日历；世界实体、背包、农田、叙事、经济仍未从
  `SaveFile` 应用。
- `NewGameHandler` 在 app 里是空实现；真实新游戏还没有重建世界/初始物品。
- 没有场景切换（传送点、`SceneStack::replace`）与过渡效果。
- 没有 26:00 强制睡眠/晕倒触发；`endDay()` 仍需外部调用。
- 死亡只进入 `GameOver`，没有掉落、销毁、复活惩罚与读档选项 UI。
- 自动存档的“安全状态”判定（战斗中/对话中/过场中挂起）未实现。
- 没有退出前“距上次存档超过 12 游戏小时”的提示；`autosaveOnQuit` 是显式开关。
- 输入上下文没有按 `AppState` 自动切换；app 仍未屏蔽暂停/对话时的玩法输入。
