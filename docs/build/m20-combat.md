# M20 Combat —— 构建记录 / 实测

对应模块文档：[20-combat.md](../modules/20-combat.md)

> **状态：已实现并三配置实测通过。** 伤害、闪避无敌、攻击窗口与去重、投射物、
> 击退、状态效果与死亡/掉落事件均已落地；app 用两个 dummy 跑通近战击杀、投射物
> 命中与中毒。

## 1. 实际落地

新增工程 `src/engine/combat/fa_combat.vcxproj`（GUID `{C0DE0014-…0014}`），
引用 `fa_core`、`fa_scene`、`fa_physics`、`fa_player`。

| 文件 | 职责 |
|---|---|
| `fa/combat/CombatTypes.hpp` | `Damage`、`AttackHitbox`、`Projectile`、`StatusEffect`、事件与 `CombatStats` |
| `fa/combat/CombatService.hpp` + `src/CombatService.cpp` | 伤害/无敌/攻击/投射物/状态/死亡事件 |
| `tests/combat.test.cpp` | `[combat]` 测试 |

### 关键实现选择

- **伤害**：只写 `player::Health`；无敌复用 `PlayerController::isInvulnerable`；
  死亡记录 `DeathEvent` + `LootSource` 字符串，不销毁实体、不掷掉落。
- **攻击**：`ActiveAttack` 保存 owner/hitbox/elapsed/hit 列表；`overlap` 查询目标层；
  同一实例对同一目标只结算一次；`elapsed` 跨过窗口也会命中。
- **投射物**：真实实体（`Transform` + `Projectile`），每步 `raycast` 找实体/瓦片，
  再 `move` 推进；命中/撞墙/寿命耗尽后销毁。没有 Collider，`move` 对无碰撞体实体
  直接平移，墙体由 `raycast` 负责。
- **击退**：`applyDamage` 调用 `PhysicsWorld::move`，不直接写 `Transform`。
- **状态**：Poison/Burn 用 `magnitude * dt` 累计，整数 tick 结算；Slow 由
  `slowFactor()` 暴露给调用方；同类刷新，不叠加。
- **事件**：四类事件缓冲由调用方 `clearEvents()`；app 每固定步累计后清空。
- **app 演示**：
  - marker 增加 `Health`（`PlayerController::attach` 不添加 Health）。
  - 两个 dummy 放在 marker 左侧开阔地（300,220 / 300,240），避免右侧墙体。
  - step 30/50 近战、step 30 投射物（向左）、step 35 中毒。
  - 近战命中两个 dummy；step 50 击杀 dummy1；投射物命中 dummy2。

### app 脚本结果

- 2 次攻击，4 次命中（每个 dummy 2 次），46 伤害。
- 1 个投射物，1 次命中，0 次撞墙。
- 1 次击杀；1 次状态 tick；9 个事件。
- dummy2 被投射物击退到 x=292。

## 2. 测试

`tests/combat.test.cpp` 覆盖：

- 伤害/死亡：20 HP 两次 10 伤害，死亡事件与 loot id、重复伤害无效。
- 无敌：`DodgeState.timeLeft` 在窗口内时伤害被拒绝，窗口结束后恢复。
- 攻击窗口：`activeFrom` 前不命中，窗口内命中一次，后续 update 不重复。
- 投射物：飞行、命中目标、销毁、事件；无目标时寿命耗尽销毁。
- 状态：Poison 0.25s tick 1 点、1s 后再 tick 5 点并到期；Slow factor 0.5 -> 1.0。
- 击退：`PhysicsWorld::move` 后 Transform 位移 12 px。

实测（三配置一致）：

```text
fa_tests.exe "[combat]" → All tests passed (50 assertions in 7 test cases)
fa_tests.exe           → All tests passed (30281 assertions in 543 test cases)
fa_app.exe --frames 60 → m20: 3 attack(s), 5 hit(s), 51 damage, 1 kill(s),
                         1 projectile(s)/1 hit(s)/0 wall hit(s), 1 status tick(s), 10 event(s)
                       → ...+M18+M20+M27 smoke test OK
fa_app.exe --play-test → 2 attack(s), 2 hit(s), 20 damage, 1 kill(s); seeds 0 -> 2
```

M20 加入两个 dummy 后，世界实体数从 5 变为 7；`m14` 摘要显示 `7 entities`。

## 3. 遇到的问题与结论

1. **脚本攻击一开始打不中。**
   marker 的起始位置是 (324,202)，不是终点 (344,242)；它在初始 30 步内向右下移动。
   step 5/12 的攻击与投射物都在够不到 dummy 的位置。把动作移到 step 30/35/50。
2. **dummy 与投射物被放在墙后。**
   marker 右侧 4 px 就是墙；dummy 放在 x=368/348、投射物从 x=352 向下，直接撞墙。
   把 dummy 移到 marker 左侧开阔地，投射物向左发射。
3. **第二次近战纵向漂移。**
   marker 在 50 步内下移约 20 px；20 px 高的 hitbox 在第二步已经够不到 dummy。
   把 hitbox 高度改为 48 px（offset y=-16）。
4. **投射物射线贴着碰撞盒左边缘。**
   射线 x 恰好等于 dummy 的 `rect.x` 时 `rayBox` 视为切线。调整 dummy 位置，让射线
   穿过盒内部。
5. **`PlayerController::attach` 不添加 Health。**
   M16 只添加 PlayerTag/Facing/State/Stamina/DodgeState；app 必须自己加
   `player::Health`，否则 `applyDamage` 永远返回 false。
6. **`CombatService` 借用物理世界。**
   必须在 `systems().clear()` 之前 `m_combat.reset()`，否则 `PhysicsSystem` 析构后
   CombatService 仍持有 `PhysicsWorld&`。
7. **投射物没有 Collider。**
   `PhysicsWorld::move` 对无碰撞体实体直接平移；墙体由 `raycast` 检测。这是有意的：
   给投射物加 Collider 会让它进入宽相并被其他查询看到。
8. **`std::optional` 头文件。**
   `CombatService.cpp` 使用 `std::optional<RayHit>`，需要显式 include `<optional>`。
9. **不依赖 M15/M18。**
   掉落表以字符串 id 事件交给组装根；M20 不 include 内容库/背包。

## 4. 验证命令

```powershell
$env:VCPKG_ROOT='D:\CPPLibraries\vcpkg'
$msbuild='C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'
foreach ($cfg in @('Debug','Development','Release')) {
  & $msbuild FloriansAdventure.sln /t:Rebuild /p:Configuration=$cfg /p:Platform=x64 /m /nologo /v:v
  & ".\build\bin\x64\$cfg\fa_tests.exe"
  & ".\build\bin\x64\$cfg\fa_app.exe" --frames 60
}
```

### 玩法验证补丁

- play 模式右键生成朝向前方的近战盒；app 消费 `DeathEvent`，按 `LootSource` 掷掉落，
  玩家靠近 14 px 内自动拾取到背包。
- `--play-test` 增加战斗段：2 次攻击击杀 dummy，确定性掉落 2 个种子。
- 脚本冒烟仍走原 M20 序列，不改 `m20:` 摘要语义。

## 5. 已知限制 / 后续

- 矩形 hitbox 不随朝向旋转；没有扇形/连击/蓄力。
- 击退直接 `move`，没有受击硬直/速度衰减；墙边可能被夹住。
- 状态同类刷新不叠加；低 dps 的 DoT 有整数 tick 延迟。
- 死亡不销毁实体、不掷掉落；需要 M15/M18/M25 接线。
- 伤害数字/特效/死亡动画由 app/M06/M10 消费事件，M20 不绘制。
- 投射物每步 raycast + move；大量投射物需要对象池/批量查询。
- 没有抗性/暴击/元素反应。
