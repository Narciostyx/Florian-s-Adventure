# M16 玩家控制器 —— 构建记录

对应模块文档：[16-player-controller.md](../modules/16-player-controller.md)

## 1. 实测环境

| 项 | 值 |
|---|---|
| MSBuild | `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe` |
| 平台工具集 | v145 / MSVC `14.51.36231` |
| vcpkg | `D:\CPPLibraries\vcpkg`，triplet `x64-windows-static-md` |
| 配置 | Debug / Development / Release × x64 |
| 新增工程 | `src/engine/player/fa_player.vcxproj` |

`fa_player` 打开 `/EHsc`，因为 `PlayerSystem.cpp` include `fa/input/Input.hpp`，
它经 `ActionMap` 拉进 `<unordered_map>`；控制器本身不抛/不捕获。

## 2. 实测结果

> 下表是 M16 初次交付的历史快照；体力消耗补丁后的最新数字见 §6.2
> （`[player]` 17 用例 / 151 断言，全量 558 用例 / 30454 断言）。

| 检查 | Debug | Development | Release |
|---|---|---|---|
| 全解决方案 `Rebuild` | exit 0，0 warning / 0 error | exit 0，0 warning / 0 error | exit 0，0 warning / 0 error |
| `fa_tests.exe` | 379 用例 / 18243 断言，全绿 | 同左 | 同左 |
| `fa_app.exe --frames 60` | exit 0 | exit 0 | exit 0 |

用例拆分（三种配置一致）：

| 过滤 | 用例 | 断言 |
|---|---|---|
| `[player]` | 14 | 107 |
| `~[player]` | 365 | 18136 |
| 全量 | 379 | 18243 |

`14 + 365 = 379`、`107 + 18136 = 18243`：`player.test.cpp` 的每个用例都带
`[player]`，没有漏标签。

## 3. 冒烟输出与对账

Debug：

```text
  m11: 1 scene depth, 4 system(s), 4 live entit(ies) (4 created / 0 destroyed) over 10 component type(s), 17 component(s), 923 extracted item(s)
       marker at (344, 282.979)
  m12: 40x23 map, 16 px tiles, 920 cell(s), 1 layer(s), 3 def(s), 920 non-empty, 143 solid, 16 tillable, 0 area(s), 0 portal(s)
       920 tile item(s) submitted, 0 culled, 0 empty, 0 without texture
  m13: 3 collider(s), 1 trigger, 114 move(s), 94 blocked, 3 cell(s) (worst bucket 1)
       114 step(s), 0 overlap quer(ies), 1 ray(s), 1 trigger begin(s) / 1 end(s), 0 event(s) last step
       nearest wall straight ahead: 4 px
  m16: move, facing east, speed 90.0, stamina 100.0, 114 update(s), 114 move / 0 idle, 0 dodge(s), 0 action(s)
  m17: target cell (19, 12), entity, 12.6 px, reachable; 1 tool use(s), 1 placement(s), 3 event(s), 1 occupied cell(s), preview valid -> invalid
M02+M03+M04+M05+M06+M07+M11+M12+M13+M16+M17 smoke test OK
```

| 观测 | 来源 |
|---|---|
| 4 系统 | PlayerSystem + PhysicsSystem + TilemapExtractSystem + SpriteExtractSystem |
| 4 实体 | 标记 + 触发器 + 板条箱 + 放置物 |
| 17 组件 | 标记 9（`Transform`/`Sprite`/`Collider`/`Velocity`/`PlayerTag`/`FacingDir`/`PlayerStateComp`/`Stamina`/`DodgeState`）+ 触发器 2 + 板条箱 3 + 放置物 3 |
| 10 组件类型 | 上面 9 种 + `Interactable` |
| 923 提取项 | 920 瓦片 + 标记 + 板条箱 + 放置物 |
| 932 quad | 923 提取项 + 9 个调试图元 |
| 3 collider | 标记 + 触发器 + 放置物 |
| 114 move / 94 blocked | 脚本化轴被归一化为 (0.868, 0.496)，走 90 px/s；到墙后持续顶墙 |
| 标记 x=344 | 墙列 22 左边界 352，标记宽 8 → 344 |
| 标记 y≈283 | 归一化后的 y 分量约 44.6 px/s × 1.9 s；由固定步数决定，会变 |
| m16 speed 90 | 走速 90；加速度在约 0.1 s 内达到目标 |
| m16 0 action | 冒烟只设置脚本化移动轴，没有模拟按键边沿 |

### Development / Release 会多跑几步

固定步数由 M04 的墙钟累积驱动；同一份代码本轮实测 Debug 114 步、
Development 84 步、Release 77 步，并行跑测试时 Debug 也见过 114 步以上。
x 始终是 344，m16 的速度/朝向/耐力在三种配置一致。不要逐字比对 y 或固定步数。

## 4. 语义决策表

| # | 决策 | 选择 | 理由 |
|---|---|---|---|
| 1 | 控制器与系统拆分 | `PlayerController` + `PlayerSystem` | 控制器可无 `IInput` 测试；系统只做输入采样 |
| 2 | 位置写入者 | 只写 `Velocity` | M13 是唯一位置写入者；闪避也用速度而不是直接 `move` |
| 3 | 动作输入 | 布尔为按下边沿，`run` 为持续 | 与 M07 的 `wasPressed`/`isDown` 语义一致 |
| 4 | 朝向 | 八向：四个正方向 + 四个斜向，按 45° 扇区 | 前四个枚举值保持历史顺序，四向消费者可 `static_cast`；斜向用 `cardinalFacing()` 折叠 |
| 5 | 闪避 | 0.25 s 速度平台，不是一帧瞬移 | 保持“只写 Velocity”的规则，碰撞照常求解 |
| 6 | 动作优先级 | UseTool > Attack > Interact | Interact 不锁移动；工具/攻击互斥 |
| 7 | 动作锁 | 0.2 s 固定 | 轻/重攻击区分留给 M20 的手感调参 |
| 8 | 耐力恢复 | 非闪避时 +20/s，动作锁期间也恢复 | 闪避是消耗窗口；攻击/工具不额外扣耐力（M20 负责） |
| 9 | 组件添加 | `attach()` 只补缺失，不覆盖 | 场景/存档可以在 attach 前设置耐力/朝向 |
| 10 | 动作 id | `PlayerSystem` 构造时缓存 | 热路径不做字符串哈希 |
| 11 | 内容 ID | 不透明 `payload` | M15 未实现 |
| 12 | 相机跟随 | 由 M06 `Camera2D::follow()` 负责 | 控制器不持有相机；app 以玩家精灵中心为目标，带死区与平滑 |

## 5. 踩坑

1. **`PlayerController.hpp` 的 `updateAnimation` 声明引用了未声明的 `Velocity`。**
   初版写了两个重载，其中一个参数是 `PlayerAnimationParams&`，且没有前向声明
   `fa::physics::Velocity`。编译器直接报未声明类型；补前向声明并删掉多余重载。

2. **`normalizeMove` 不能只归一化长度 > 1 的向量。** M07 的 `moveAxis()` 已经归一化，
   但冒烟的脚本轴 `{0.35, 0.2}` 长度只有 0.4。最初写成“长度 > 1 才归一化”，
   结果脚本速度只有 36 px/s。正确做法是任意非零向量都归一化。

3. **UseTool 的动作锁会立刻把速度压到 0。** 测试最初在 `UseTool` 后断言
   `velocity.x > 0`，但动作锁分支会减速到零。断言改成 0，并在锁结束后再验证移动。

4. **攻击拒绝的测试顺序写反了。** 先跑了 20 步让动作锁过期，再断言攻击被拒绝，
   实际会成功。改成“锁内攻击 → kBusy；等锁过期 → 攻击成功”。

5. **`actionLock` 在步首递减。** 如果剩余时间小于 `dt`，这一步就会进入正常移动分支；
   测试用 20 步（0.333 s）覆盖 0.2 s 的锁，避免边界抖动。

6. **无敌窗口用 `elapsed = duration - timeLeft` 计算。** 直接看 `timeLeft` 会把方向
   弄反；窗口是 [0.05, 0.20] 秒，从闪避开始算。测试在 0.0167 s、0.117 s、0.217 s
   三个点验证。

7. **闪避期间不能再闪避/用工具/攻击。** 初版把 `dodging` 在闪避启动前算好，
   导致同一帧既能启动闪避又能设置 UseTool。改成闪避启动后再算 `dodgingNow`，
   工具/攻击看这个值。

8. **成员顺序与管线顺序。** `PlayerSystem` 必须在 `PhysicsSystem` 之前注册；
   `PlayerController::attach` 必须在 `onEnter` 创建系统前调用。冒烟里
   `attach` 放在 `buildWorld` 的标记创建处。

9. **app 的标记是 8×8，不是真实 16×24 角色。** 控制器不关心尺寸；真实玩家应使用
   `kPlayerFeetCollider`（10×6，offset `{3,18}`）。文档里明确标注这个差异，避免
   误以为冒烟使用了脚部盒。

10. **绑定属于 composition root。** M07 不知道 `Run`/`Dodge`/`UseTool` 等玩法动作；
    `PlayerTypes.hpp` 只提供名字，`SmokeSink` 负责注册和绑定。不要把这些绑进 M07。

11. **`/EHsc` 判定仍看 include 图。** `fa_player` 的控制器部分不需要异常，但
    `PlayerSystem.cpp` include `fa/input/Input.hpp`，间接拉进 `<unordered_map>`，
    所以工程级打开 `/EHsc`；没有代码 throw/catch。

## 6. 复现命令

```powershell
$env:VCPKG_ROOT = 'D:\CPPLibraries\vcpkg'
$msbuild = 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'

foreach ($cfg in @('Debug','Development','Release')) {
    & $msbuild FloriansAdventure.sln /t:Rebuild /p:Configuration=$cfg /p:Platform=x64 /m /nologo /v:m
    & ".\build\bin\x64\$cfg\fa_tests.exe"
    & ".\build\bin\x64\$cfg\fa_app.exe" --frames 60
}

& ".\build\bin\x64\Debug\fa_tests.exe" "[player]"
```

单编新工程：

```powershell
& $msbuild 'src\engine\player\fa_player.vcxproj' /t:Rebuild `
    /p:Configuration=Debug /p:Platform=x64 /p:SolutionDir="$($PWD.Path)\" /m /nologo /v:m
```

## 6.1 八向朝向（v1.1 补充）

- `Facing` 追加 `NorthEast`/`SouthEast`/`SouthWest`/`NorthWest`；前四个枚举值
  保持 `North=0, East=1, South=2, West=3`，现有四向消费者仍可 `static_cast`。
- `facingFromVector()` 改为按 `atan2` 的 45° 扇区量化，边界正好在距正方向 22.5°；
  零向量仍返回 fallback。
- `directionVector()` 为斜向返回单位对角 `(±1/√2, ±1/√2)`，因此斜向闪避仍保持
  `dodgeDistance/dodgeDuration` 的总速度。
- 新增 `isDiagonal()` 与 `cardinalFacing()`（斜向折叠时纵向优先）。
- `[player][facing]` 覆盖八向选择、22.5° 边界、四个斜向单位向量与折叠；
  app 摘要的 `facing` 现在会打印 `south-east` 等斜向。
- M10 的 `Direction` 仍是四向；斜向动画需要在组装根映射到相邻主方向。

## 6.2 体力消耗（v1.2 补充）

- `PlayerTuning` 新增 `useToolStaminaCost`（默认 3）、`attackStaminaCost`（默认 4）、
  `runStaminaPerSecond`（默认 10）；`dodgeStaminaCost`/`staminaRegenPerSecond` 保持。
- 奔跑只在移动且耐力 > 0 时生效；耐力为 0 时降为走速，按住奔跑不恢复，松开后恢复。
- 工具/攻击在 `actionLock` 期间不回复耐力，避免 0.2 s 的短锁把 3/4 点消耗抵消。
- 动作被拒绝时记录 `ActionRejected`；app 对 `kInsufficientStamina` 显示
  `NOT ENOUGH STAMINA`。
- app 从 `config/game.json` 的 `stamina` 段读取 `max`、`regen_per_second`、
  `run_drain_per_second`、`dodge_cost`、`attack_cost`、`tool_cost`，启动时打印。
- 测试：`[player]` 17 用例 / 151 断言；全量 558 用例 / 30454 断言（三种配置一致）。

## 7. 遗留风险

1. 冒烟标记是 8×8；真实角色需要 `kPlayerFeetCollider`/16×24 精灵。
2. 动作锁固定 0.2 s，未区分轻/重攻击或不同工具。
3. 闪避期间完全禁止其他动作，是否允许取消攻击未定。
4. 多玩家共享同一 `PlayerIntent`；v1 只支持一个玩家。
5. `Hurt`/`Menu` 状态保留但未进入；需要 M09/M20/M25。
6. 相机跟随由 M06 实现；控制器本身仍不感知相机。
7. `payload` 是临时内容 ID；M15 落地后统一替换。
8. 动作请求只记录不发布；M03 事件化由组装根决定。
9. 没有动画；M10 落地后消费 `PlayerAnimationParams`。
10. 没有攻击/工具命中判定；M17/M20 负责。
