# M21 AI —— 构建记录 / 实测

对应模块文档：[21-ai.md](../modules/21-ai.md)

> **状态：已实现并三配置实测通过。** 感知、A* 寻路、idle/patrol/chase/attack/
> flee/return 状态机、M13 移动与 M20 攻击执行均已落地；app 用一个独立 agent 与
> 静止假人跑通追击和攻击，不干扰 M20 的脚本目标。

## 1. 实际落地

新增工程 `src/engine/ai/fa_ai.vcxproj`（GUID `{C0DE0017-…0017}`），引用
`fa_core`、`fa_scene`、`fa_physics`、`fa_tilemap`、`fa_combat`、`fa_player`。
因为公共头经过 `World`/`PhysicsWorld` 间接包含 `std::unordered_map`，本工程
单独打开 `/EHsc`。

| 文件 | 职责 |
|---|---|
| `fa/ai/AiTypes.hpp` | `AiState`、`AiTargetTag`、`BehaviorProfile`、`AiAgent`、`AiStats` |
| `fa/ai/AiService.hpp` + `src/AiService.cpp` | A*、视线、感知、状态机、移动、攻击 |
| `tests/ai.test.cpp` | `[ai]` 11 用例 / 101 断言 |

### 关键实现选择

- **服务而不是 ECS 系统**：`AiService` 不是 `scene::ISystem`。组装根在固定步按
  自己的顺序调用 `update(world, dt)`，测试不需要场景栈或帧循环。
- **`AiAgent` 保持纯数据**：路径不进 ECS；`AiService` 用私有 `PathState` 按
  `EntityId` 保存节点、索引与 goal。这样组件可平凡拷贝，空闲 agent 不占堆。
- **A***：4 邻接、Manhattan 启发、4096 节点上限、确定性 tie-break；路径重建后
  用视线做一次平滑，去掉能直连的中间格。不可达目标返回 false。
- **视线**：Bresenham 穿过 `Tilemap::isSolidCell`，起点和终点本身允许是实心格，
  这样墙后目标不可见，但同格目标仍然可见。
- **感知节流**：默认每 6 个固定步扫一次；只考虑带 `AiTargetTag` 的活实体，
  按距离取最近，并要求视线通过。M21 不依赖 M16：玩家由组装根打上
  `AiTargetTag`，而不是直接读 `PlayerTag`。
- **状态机**：Idle/Patrol/Chase/Attack/Flee/Return。`Attack` 只在范围内执行，
  冷却由 `BehaviorProfile::attackCooldown` 控制；离开范围回到 `Chase`。
- **移动**：沿路径朝格中心移动，调用 `PhysicsWorld::move`，因此墙体和滑动由 M13
  负责；没有 Collider 的 agent 仍可移动（测试路径）。
- **攻击**：只调用 `combat::ICombatService::spawnAttack`，不直接扣血；目标层由
  `BehaviorProfile::attackMask` 决定，默认 Player，demo 用 Enemy 打静止假人。
- **失败回退**：寻路失败时若目标可见则直线追击，否则回 `Idle`/`Return`，
  不阻塞其他 agent。
- **app 集成**：
  - DemoScene 新增一个紫色 agent（255,180）和一个蓝色静止假人（300,180）；
    假人带 `AiTargetTag`，与 M20 的两个 dummy 分开，避免移动脚本目标。
  - `m_ai` 在 `onEnter` 里用地图/物理/战斗构造，`onExit` 先于 combat 释放。
  - 固定步在 `advanceCombat` 之后调用 `advanceAi`；摘要新增 `m21:` 行，
    smoke OK 加入 `+M21`。

### 工程文件

`fa_ai` 的源文件和公共头显式列在 `.vcxproj`；`fa_tests.vcxproj` 增加
`ai.test.cpp` 与 `fa_ai` 引用，`fa_app.vcxproj` 增加 `fa_ai` 引用，
`.sln` 增加工程与三配置映射。

## 2. 测试

`[ai]` 覆盖：

- A* 绕过墙体、不可达目标、路径节点均为可行走格。
- 视线被实心格阻挡、开阔直线可见、同格可见。
- 感知找到范围内可见目标；墙后/超距不可见。
- 追击产生移动与路径统计。
- 进入攻击范围后攻击；冷却期间不重复；冷却后可再次攻击。
- 丢失目标后返回出生点并回到 Idle。
- 低血量进入 Flee；无目标时 Patrol 会生成路径。
- `clear()` 重置统计。

实测：

```text
fa_tests.exe "[ai]" → All tests passed (101 assertions in 11 test cases)
fa_tests.exe        → All tests passed (29317 assertions in 500 test cases)
fa_app.exe --frames 60 → m21: attack, target valid (visible), 1 path node(s), 1 agent(s),
                         9–10 perception(s), 9–10 target(s), 2 path(s), 38 move(s),
                         1 attack(s), 4 state change(s)
                       → M02+…+M20+M21+M22+M27 smoke test OK
```

## 3. 遇到的问题与结论

1. **Idle → Chase 不会在同一步继续执行 Chase 分支。** `switch` 在状态变化后
   `break`，所以第一次 update 只进入 Chase，下一步才判定攻击范围。测试按这个
   语义调整；如果要“一步到位”，需要在状态机外层加循环，v1 不需要。
2. **`PhysicsWorld::move` 是 `[[nodiscard]]`。** 直接丢弃返回值触发 C4834，
   并在 `/WX` 下变成错误；两处移动调用改为 `static_cast<void>(...)`。
3. **home 必须是格中心。** `Return`/`Flee` 用世界坐标比较 `home`；若把
   `Transform::position`（左上角）直接当 home，agent 永远差 8 px 无法回 Idle。
   测试与 app 都把 home 设为格子中心。
4. **攻击目标层要可配置。** 默认 `Player`；demo 的假人是 `Enemy`，所以
   profile 把 `attackMask` 改成 Enemy。否则 AI 只会空挥。
5. **AI 攻击会进入 M20 统计。** `CombatService::spawnAttack` 是唯一入口，
   所以摘要里的 M20 attacks 从 2 变成 3；这是预期行为，不是脚本重复。
6. **嵌套 `world.each` 安全。** 外层遍历 agent、内层遍历目标，二者都不做结构
   变更；路径容器在 service 内，不触发 ECS 重分配。
7. **A* 平滑可能产生对角线。** 视线平滑允许跳过拐角；实际移动仍由 M13 的
   轴分离滑动处理，没有穿墙。

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

- 没有寻路任务池/每帧上限；大量 agent 同时重算会推高固定步耗时。
- 没有听觉、区域仇恨、群体编队、包围或召唤。
- 没有 NPC 日程；M22 的 `HourChanged` 接入留给 M23/M25。
- AI 存档由 P0-3 接入：`AiAgentSnapshot` 保存状态/计时器，目标用稳定 role 解析；
  读档后路径通过 `clearPaths()` 重建。
- 没有巡逻点列表、动态障碍重算、卡角落脱困或传送回出生点。
- `Flee` 目前只往 home 跑，没有逃跑方向评分。
- app 没有 AI 路径/视野调试绘制；M27 后续可读 `AiStats` 与 `AiAgent`。
