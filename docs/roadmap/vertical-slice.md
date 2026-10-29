# 首个垂直切片

垂直切片的目标不是“做完一个系统”，而是用最小内容验证**完整玩家体验**：一天之内，玩家能种地、能战斗、能存档。

> **当前状态（玩法验证第十九增量，2026-10-27 实测）**：28 个模块全部实现；
> `fa_tests.exe` 564 用例 / 30543 断言全绿，Debug / Development / Release × x64
> 零 warning，`--frames 60` 与 `--play-test` 三配置 exit 0。
>
> **本轮更新**：完成 **P2-2a 工程收尾**——存档 v1→v2 迁移、CI Debug/Release 矩阵
> （测试 + frames + play-test + soak + 1080p）、格式检查修复与全量格式化、
> `--soak`/`--resolution` 诊断开关。
>
> **上一增量**：玩家与敌人攻击动画——3 帧挥砍（`tex.effect.slash`）由 M10
> 播放，命中判定改由动画 `hit` 事件触发；M21 新增 `setAttackHandler()`，敌人攻击
> 先起手再在 `hit` 帧生成命中盒。`--play-test` 断言玩家“起手不伤害、`hit` 帧才掉血”，
> 以及敌人“进入 Attack 后先出现 swing、起手帧不伤害、随后才命中”。
>
> **上一增量**：P2-1 内容补齐——3 作物（萝卜/土豆/草莓）、2 敌人（史莱姆/蝙蝠）、
> 12 物品、8 个程序化 SFX + 1 首循环音乐、22 张纹理；敌人数量/AI 参数/纹理全部由
> JSON 驱动。格式见 [内容 JSON 格式](../content-format.md)。
>
> 已完成的 play 闭环：叙事接入、体力/坐标、武器伤害、食物使用、加工入口、
> 死亡菜单/读档、叙事/AI/世界对象存档、敌人反击与命中反馈、商店/任务/背包 UI、
> 内容补齐（3 作物/2 敌人/12 物品/8 SFX/1 音乐）。P2-2 已完成存档迁移、CI 配置、
> soak/分辨率基线、1000 精灵压力基线与真实 30 分钟 soak；仅剩 CI 首次 Actions 实跑。
>
> 可手动游玩（`--window --play`）：启动进入**主菜单**（新游戏/继续/存档槽/退出），
> `Esc` 打开暂停面板（继续/保存/存档槽/主菜单）。`WASD` 移动，`J` 打开任务日志，
`I` 打开分类/分页背包；快捷栏 1–3 是
> 锄头/浇水壶/种子，左键使用并自动收获成熟作物；右键近战，敌人死亡掉落并自动拾取；
> 床边 `E` 睡觉；橙色**出货箱**旁 `E` 把所有作物送进出货箱，次日结算金币；
> 紫色**商人**旁 `E` 打开商店，可买种子、卖萝卜；农场 **Elder** 旁 `E` 打开对话，
> `1/2` 选择、`Enter` 继续，可接取“A Turnip for the Elder”任务；`I` 打开**背包面板**，
> 点击 MOVE 在背包与快捷栏之间移动 1 个物品；HUD 同时显示生命、耐力与任务状态。
>
> 内容/商店/叙事/资源目录已移到 `data/*.json`，地图布局在 `data/maps/*.json`
> （`farm.json` / `farm-scripted.json` / `forest.json`），play 实体由
> `data/entities.json` 生成；窗口/UI 缩放/玩法参数在 `config/game.json`，缺失时回落默认值。
> 纹理优先读 `assets/textures/**/*.png`（stb_image），缺图/解码失败回落到程序占位。
> 主菜单等面板居中，字号由 `ui.scale`（默认 1.25×）放大；窗口最小 1280×720、可放大。
>
> HUD 已去掉多余的 `PAUSE` 按钮（`Esc` 仍暂停），日期/时间/季节/区域移入左侧
> HUD 下方。新增湿土、萝卜 4 阶段（种子/幼苗/成苗/成熟）、箱子、史莱姆贴图；
> 作物阶段由 `data/content.json` 的 `seed_sprite`/`stage_sprites` 决定，
> 5 天成熟与 3 个 `stage_days` 不变。
>
> 地图在 play 模式拆成两张独立的 **40×23** 地图：农场与林地。门口 `portalAt`
> 命中后由 `SceneStack::replace` 真正替换场景，农田/背包/叙事/玩家生命耐力放在
> 组装根的 `GameState` 中，因此过图不会丢状态；HUD 显示 `FARM`/`FOREST`。
> 林地有两个会主动索敌/攻击的史莱姆与**荆棘**（3 格）：站在荆棘上每 0.7s 扣 5 点生命，
> 生命归零会晕倒、切回农场床边复活，并扣除 10% 金币。地上的掉落物与当前地图
> 随存档写入并读回。scripted smoke 仍使用 40×23 农场图。
>
> 睡眠规则：任意时间可睡，醒来固定为次日 08:00。睡满 8 小时恢复全部体力；
> 不足 8 小时按 `(实际小时 / 8)²` 递减，最低保留 1%。
>
> 已持久化并读回：日期/时间/天气、当前地图、玩家位置/朝向/耐力、背包/快捷栏/装备、
> 农田与作物状态、金币、出货箱待结算清单、加工任务与每日商店余量。
> `--play-test` 无头串起对话 → 接任务 → 种植 → 5 天 → 收获（任务完成+奖励）→
> 补种 → 战斗/掉落 → 睡眠（8h/7h/1h 恢复档位）→ 商店买卖 → 出货结算 →
> 农场门/双向换图 → 保存 → 主菜单 → 读档 → 暂停/存档槽，逐项校验后返回失败码。
>
> 已可玩的叙事闭环：农场 Elder 对话 → “I will bring a turnip.” 接任务 →
> 收获萝卜 → 自动完成 → 奖励 2 个种子 + 10 金币；HUD 显示 `QUEST ...` /
> `QUEST COMPLETE`。对话期间 M25 进入 Dialogue，时钟与玩法暂停，结束后恢复。
>
> 仍缺：攻击动画/真实素材、背包拖拽/排序；
> 音频是 8 个程序化 SFX + 1 首程序化循环曲，不是真实素材；
> 1920×1080 启动 smoke、1000 精灵压力基线（p50 16.95 ms）与真实 30 分钟 soak
> （183 次存读档、实体/纹理无增长）均已通过。
>
> 当前实际内容为 **12 物品 / 3 作物 / 1 配方 / 2 敌人 / 3 掉落表 / 22 纹理 /
> 8 SFX + 1 音乐**，已达到原定切片的内容数量；格式见
> [内容 JSON 格式](../content-format.md)。

### 立即运行

```powershell
# 可操作种植会话（--window 现在默认进入 play；--play 仍可显式指定）
.\build\bin\x64\Debug\fa_app.exe --window --play

# 无头自检：种植 + 5 天生长 + 收获 + 补种 + 战斗/掉落 + 睡眠档位 + 存档/菜单往返
.\build\bin\x64\Debug\fa_app.exe --play-test
```

操作：主菜单点「新游戏/继续/存档槽」；`WASD` 移动，`1–9` 选快捷栏，滚轮循环，
左键使用当前工具/种子，右键攻击（伤害来自装备武器），`Shift` 奔跑（持续耗体力），
`Space` 闪避（消耗体力），`F` 食用选中的汤（+20 HP / +30 体力），
床边 `E` 睡觉，橙色出货箱 `E` 出货，紫色商人 `E` 开商店（`COOK SOUP` / `COLLECT`），
农场 Elder 旁 `E` 对话（`1/2` 选择、`Enter` 继续），`I` 开背包，
`Esc` 关闭面板/暂停，F3 统计（含角色世界/格子坐标），反引号控制台；
林地西侧的棕色**荆棘**会持续扣血，生命归零会在床边复活并损失 10% 金币；
控制台内 `time.addDays N` / `time.set HH:MM` 推进时间。
数据/配置/图片分别在 `data/`、`config/`、`assets/`；删除任意文件都会回落到内置默认值。
数据 JSON 的字段说明见 [内容 JSON 格式](../content-format.md)。

### 当前实测

> 2026-10-27 三配置复测：Debug / Development / Release 的 `fa_tests`、
> `--frames 60` 与 `--play-test` 均通过；本轮完成季节库存/价格波动、任务日志与
> 分类/分页背包。

- `fa_tests.exe`：562 用例 / 30521 断言全绿（Debug / Development / Release）。
- `fa_app.exe --frames 60`：三配置 exit 0，smoke 覆盖 M02–M27（Release 不含 M26/M27）；
  scripted 标记仍停在 x=344，说明 scripted 漂移未被移除。
- `fa_app.exe --play-test`：三配置 exit 0，输出
  `stamina max 100.0 -> ok`、`weapon damage 12 (content 12) -> ok`、
  `content 12 item(s), 3 crop(s), 2 enemy(ies), 3 loot table(s) -> ok`、
  `audio 8 sfx, looping music ok -> ok`、
  `attack animation start ok, wind-up ok, hit ok -> ok`、
  `forest attack animation ok`、
  `food hp 50 -> 70, stamina 50 -> 80.0, soup 3 -> 2 -> ok`、
  `craft soup ok (ready ok, collect ok), soup 2 -> 3 -> ok`、
  `save-state ai ok, enemy ok, debris ok, placed ok, narrative ok, health ok -> ok`、
  `quest log ok`、`inventory filter ok`、
  `shop season ok, price swing ok -> ok`、
  `enemy counterattack chase ok, attack ok, swing ok, wind-up ok, hurt ok -> ok`、
  `combat death ok, player intact ok, respawn ok -> ok`、
  `thorns death menu ok, respawn ok, gold 154 -> 139 -> ok`、
  `death load menu ok, loaded ok -> ok`、`death main-menu ok -> ok`、
  `dialogue ok, quest accepted ok`、
  `turnips 0 -> 1, quest complete, reward gold 100 -> 110, seeds 19 -> 21 -> ok`、
  `sleep wake 08:00, 8h 100.0, 7h 76.6, 1h 1.6`、
  `shop gold 170, shipped 3, settled +51`、`props shipping ok, merchant ok`、
  `farm gate ok, portal farm->forest ok, forest->farm ok, forest dummy ok`、
  `inventory move ok, panel ok/ok, health 100`、`thorns death ok, gold 171 -> 154`、
  `load marker ok, bag ok, day ok, farm ok, map ok, gold ok, loot ok, forest ai ok` 与
  `menu paused ok, slot listed ok, menu ok, resumed ok`。
- play 模式场景栈摘要为 `1 push(es), 0 pop(s), 12 replace(s)`；每张地图的 M12 摘要
  是 `40x23 map ... 1 portal(s)`（农场或林地），scripted smoke 仍是
  `40x23 map ... 0 portal(s)`。
- 启动打印 `config: window 1280x720 (min 1280x720), ui scale 1.25, 9 entity spawn(s)`；
  M05 为 `22 catalog entr(ies), 22 texture(s) cached, 0 placeholder(s)`（PNG 解码成功），
  M15 为 `12 item(s), 3 crop(s), 1 recipe(s), 2 enemy(ies), 3 loot table(s)`；
  `play-test` 额外校验 `9 entity spawn(s), bed from table ok, bat spawn ok` 与
  `crop sprites ok, detail textures ok`。

## 1. 切片目标

玩家在一个小型农场与相邻林地中：

1. 从主菜单开始，查看 HUD（日期、时间、金钱、生命、耐力）——**已完成**：
   主菜单/存档槽已接，HUD 同时显示生命/耐力/日期/时间/区域。
2. 用锄头开垦 3 块地，播下萝卜种子，浇水——**已可手动完成**（快捷栏 1–3 + 左键）；
   浇水后显示湿土贴图，萝卜按种子/幼苗/成苗/成熟 4 张阶段贴图显示。
3. 前往林地，用剑击败 2 只史莱姆，拾取掉落物——**已可手动完成**：穿过农场东侧门，
   `SceneStack::replace` 把场景换成独立的林地地图，林地里有两个会追击/攻击玩家的
   AI 史莱姆；掉落拾取复用 M20。
4. 返回农场，收获已成熟作物（可通过调试命令推进时间）——**已可手动完成**；`time.addDays` 已提供。
5. 在出货箱卖出作物，获得金钱——**已可手动完成**：橙色出货箱旁 `E` 把作物加入待结算
   清单，次日结算到金币；紫色商人 `E` 可当场买卖（买种子、卖萝卜）。
6. 回家睡觉，触发日终结算与自动存档——**已可手动完成**：任意时间在床边按 `E`，
   醒来固定次日 08:00；睡满 8 小时回满体力，不足按 `(小时/8)²` 衰减、最低 1%。
   随后 `GameFlow::endDay()` → M22 推进 → M19 生长 → M24/M23 结算 → 自动存档。
7. 退出并重新启动，读档回到次日早晨——**已可手动完成**：主菜单「继续」加载 autosave
   （或在存档槽面板选择槽位）；位置、朝向、耐力、背包/快捷栏/装备、农田、
   日期/时间/天气与金币/出货清单/加工任务都写回并应用。

## 2. 范围与实际落地

### 2.1 原定切片范围（验收基线）

> 以下是原定基线；“不包含”里有些能力后来已在引擎层实现（季节、任务、加工），
> 但 play 可达性不同，以 2.2 为准。

**原定包含**

- 一张农场地图（约 40×30 瓦片）+ 一张林地地图（约 40×30）。
- 3 种作物：萝卜（4 天）、土豆（6 天）、草莓（8 天，再生）。
- 1 种工具：锄头；1 种武器：木剑；1 个浇水壶。
- 2 种敌人：史莱姆（近战）、蝙蝠（飞行/冲刺）。
- 1 个 NPC：商人（对话 + 商店）。
- 基础 HUD、背包、快捷栏、暂停菜单、主菜单。
- 日/夜循环、晴天/雨天。
- 单槽存档 + 自动存档。
- 基础音效与一首背景音乐。

**原定不包含**

- 季节系统（切片固定为春季）。
- 任务系统、好感度、节日。
- 加工、酿造、动物、钓鱼。
- 多地图大地图、传送网络。
- 手柄支持（架构预留，切片可不验证）。
- 编辑器（用 JSON 手工编辑地图）。

### 2.2 当前实际范围（2026-10-23）

| 能力 | 状态 | 说明 |
|---|---|---|
| 农场 / 林地两张独立地图 | ✅ 已落地 | 40×23，JSON 驱动，`SceneStack::replace` 切换 |
| 种植（锄 / 浇 / 种 / 收） | ✅ 已落地 | 3 种作物（萝卜 5 天 / 土豆 6 天 / 草莓 8 天可再生）；季节、温室、天气生长规则已接 |
| 近战与掉落 | ✅ 已落地 | 玩家近战伤害读取装备武器；林地 2 个会追击/攻击的 AI 史莱姆；掉落自动拾取 |
| 日历 / 季节 / 天气 / 光照 | ✅ 已落地 | M22；HUD 显示日期/时间/季节/区域 |
| 体力 / 奔跑 / 闪避 | ✅ 已落地 | 奔跑、闪避、攻击、工具消耗体力；`stamina` JSON 可配；HUD 显示耐力 |
| 调试坐标 | ✅ 已落地 | F3 显示玩家世界坐标与格子坐标 |
| 睡眠 / 日终 / 自动存档 | ✅ 已落地 | 睡眠恢复曲线、日终结算、autosave |
| 主菜单 / 暂停 / 存档槽 | ✅ 已落地 | 鼠标操作；键盘焦点链未做 |
| 存档 / 读档 | ✅ 核心闭环 | 日期/位置/背包/农田/经济/掉落 + 叙事/AI/世界对象/玩家生命；读取固定重建场景 |
| 商店 / 出货箱 | ✅ 基础版已接 play | 买卖萝卜/种子、次日结算；库存条目显示价格/每日余量/季节可用性，价格按日确定性波动 |
| 背包 / 快捷栏 / 装备 | ✅ 基础版已接 play | 分类过滤 + 上下页 + 滚轮滚动，每页 8 个背包堆叠；仍无拖拽/排序 |
| NPC 对话 / 任务 | ✅ 基础版已接 play | 1 个 Elder、1 条任务；`J` 任务日志显示进行中/已完成与目标进度；无多 NPC |
| 加工 / 配方 | ✅ 基础版已接 play | 商店 `COOK SOUP` 消耗 2 萝卜，30 游戏分钟后 `COLLECT`；无设备实体/燃料/队列上限 |
| 食物 / 消耗品 | ✅ 基础版已接 play | `F` 食用选中食物；汤 +20 HP、+30 体力；效果由 JSON 字段配置 |
| 死亡 | ✅ 已落地 | 10% 金币只扣一次；GameOver 面板：复活 / 读自动存档 / 主菜单 |
| 战斗深度 | 🟡 基础版 | 玩家与林地敌人都是 3 帧挥砍，命中由动画 `hit` 事件触发；敌人主动索敌/追击/攻击，受击闪烁、伤害数字与命中/受伤 SFX；仍无敌人专属招式/受击硬直 |
| 音频 | 🟡 占位素材 | `data/audio.json` 提供 8 个 SFX（锄/浇/种/收/拾取/命中/受伤/购买）+ 1 首循环音乐；程序化音色，无真实素材/流式 |
| 美术 | 🟡 占位 | 31 张 AI 生成的像素 PNG（`assets/textures/<类别>/`）+ 程序化回落；catalog 驱动，新增贴图无需改代码 |
| 手柄 | ❌ 未做 | 架构预留 |
| 动物 / 钓鱼 / 节日 | ❌ 未做 | 不在 v1 切片 |

> 结论：**可玩闭环、战斗反馈与信息呈现都已成立。** 下一阶段优先补内容量
> （作物/敌人/物品/音频/美术），而不是继续堆 UI。

## 3. 内容清单（原定 vs 当前）

| 类型 | 原定 | 当前实际 | 差距 |
|---|---|---|---|
| 地图 | 2 | 2（农场 / 林地，40×23） | 尺寸小于原定 40×30 |
| 瓦片集 | 2 | 1 套（草地 / 墙 / 土 / 荆棘） | 林地复用同一套 |
| 作物 | 3 | 3（萝卜 / 土豆 / 草莓，草莓可再生） | 达到原定数量；无品质/产量表 |
| 物品 | 12 | 12（作物种子/产物、汤、锄头、水壶、剑、史莱姆凝胶、蝙蝠翼） | 达到原定数量；无更多武器/材料层级 |
| 敌人 | 2 | 2（史莱姆 / 蝙蝠，均为林地 AI） | 达到原定数量；无 idle/move/attack/hurt/death 动画 |
| NPC | 1（对话 + 商店） | 1 个商店商人 + 1 个可对话 Elder（1 条任务） | 多 NPC 与商店对话未做 |
| 音效 | 8 | 8（锄地/浇水/播种/收获/拾取/命中/受伤/购买） | 程序化占位音，非真实素材 |
| 音乐 | 1 | 1 个循环曲（`music.day`） | 程序化占位音，非真实音乐 |
| 配方 / 加工 | 1（汤） | 1 个配方 + 加工服务 + 商店 `COOK SOUP`/`COLLECT` | 无设备实体、燃料、队列上限 |

## 4. 完成定义（Definition of Done）

功能

- [x] 上述 7 步流程可从头到尾完成，无阻塞性 bug（`--play` 手动 + `--play-test`）；
  农场/林地是两张独立地图，由 `SceneStack::replace` 切换。
- [x] 核心存档往返一致（地图、位置/朝向/耐力、背包/快捷栏/装备、作物、
  日期/时间/天气、金币/出货清单/加工任务/每日商店余量、地面掉落物）。
- [x] 玩家死亡后能复活且不崩溃：荆棘扣血 → 10% 金币（只扣一次）→ GameOver
  面板 → 复活（切回农场床边）/ 读取自动存档 / 主菜单。
- [x] 所有内容由 JSON 定义，改数值不需重新编译（物品/作物/敌人/掉落/商店/叙事/
  实体生成/地图布局/窗口/玩法/体力参数已数据驱动；地图文件缺失时回落内置布局）。
- [x] 体力消耗可配：`stamina.max/regen_per_second/run_drain_per_second/
  dodge_cost/attack_cost/tool_cost`；F3 显示玩家世界与格子坐标。
- [x] 武器伤害来自 `ItemDef::damage`；食物效果来自 `heal_amount`/`stamina_restore`；
  加工可从商店面板完成。
- [x] 敌人会反击；受击闪烁与伤害数字已接。
- [x] 玩家与敌人攻击动画：3 帧挥砍，命中盒由 `hit` 帧事件生成。
- [ ] 更深的战斗/加工表现：敌人专属招式/受击硬直、设备实体与队列上限。
- [x] NPC 对话、任务接取/完成/奖励在 play 中可完成（1 个 Elder / 1 条任务）。
- [x] 生命归零进入死亡菜单（复活 / 读自动存档 / 主菜单），三条路径均通过
  `--play-test`。
- [x] 叙事（好感/任务/flags）、AI（agent 状态/目标）与世界对象（敌人/杂物/放置物）
  写入存档并读回；`--play-test` 用“保存→改状态→读档”逐项断言。
- [x] 内容达到原定切片：3 作物、2 敌人、12 物品、8 音效、1 首音乐
  （音频仍为程序化占位音，见内容清单）。
- [ ] 商店/任务/背包达到可用：季节库存、任务日志、分类/滚动背包。

质量

- [x] Debug / Development / Release 构建零警告（`/W4 /WX`）。
- [x] 三配置 `fa_tests.exe`、`--frames 60` 与 `--play-test` 全绿/exit 0。
- [ ] 1920×1080 与 1280×720 下像素显示正确（两种尺寸的启动 smoke 已通过，
  像素级截图对比未做）。
- [ ] 稳定 60 FPS（目标硬件：集成显卡）。
- [x] 模拟 30 分钟无增长：`--soak 1800` 跑 183 次存读档，entities/textures 稳定；
  真实人工游玩 30 分钟仍未做。
- [ ] 无未处理异常/崩溃；错误均有日志（目前只证明短时 smoke 无崩溃）。

体验

- [ ] 新玩家无需说明即可完成“耕地→播种→浇水”。
- [x] 攻击有明确命中反馈（受击闪烁 + 伤害数字；音效/特效仍缺）。
- [x] 敌人会反击，战斗不是单向输出。
- [ ] 交互目标高亮清晰，非法操作有反馈。
- [ ] 对话/商店/背包/死亡菜单的键盘与鼠标焦点明确。

## 5. 实施顺序

1. 骨架：M00→M01→M02→M04（可运行空窗口）。
2. 表现：M05→M06→M07（精灵 + 输入）。
3. 世界：M11→M12→M13→M10（地图 + 移动 + 碰撞 + 动画）。
4. 种植：M15→M18→M17→M19（内容 + 背包 + 交互 + 作物）。
5. 存档：M14（单槽 + 自动存档）。
6. 时间：M22（日期 + 天气；后来扩到四季与天气权重）。
7. 战斗：M20→M21（攻击 + 敌人）。
8. 流程：M25→M09（菜单 + HUD）。
9. 打磨：M08→M27（音频 + 调试 + 性能）。

## 6. 切片验收测试脚本

1. 启动游戏 → 主菜单 → 新游戏。
2. 确认 HUD 显示第 1 天 8:00、金币 500、生命/耐力 100。
3. 走到农场 Elder 旁按 `E`，选择「I will bring a turnip.」，确认 HUD 出现
   `QUEST A TURNIP FOR THE ELDER`，对话结束后恢复操作。
4. 走到田地，按使用键锄 3 格，确认出现耕地贴图。按 `I` 打开背包，切换
   `SEED/CROP/FOOD/TOOL` 分类并用 `PREV/NEXT` 或滚轮翻页，确认列表随分类变化。
5. 选择萝卜种子，播 3 格，确认种子消耗、作物出现。商店购买土豆种子/草莓种子
   （春季上架），分别播种；推进到成熟确认土豆 6 天、草莓 8 天，草莓收获后
   `regrow_days=3` 会重新结果。
6. 切换浇水壶，浇 3 格，确认湿土贴图。
7. 穿过农场 x=22 的门走到右侧，再从 x=39 的东门进入林地；确认场景替换为林地，
   HUD 显示 `FOREST`。
8. 接近林地史莱姆/蝙蝠，确认它们会追击并攻击玩家（受击闪烁 + `-N` 伤害数字）；
   击杀后确认史莱姆掉种子/凝胶、蝙蝠掉蝙蝠翼，并可拾取。
9. 返回农场；用调试命令 `time.addDays 4` 推进到成熟，收获萝卜；确认
   `QUEST COMPLETE`、金币 +10、种子 +2。
10. 在商店确认每个条目的今日价格/`STOCK n`；若把季节推进到夏季/秋季，确认汤显示
    `OUT OF SEASON` 且没有购买按钮。按 `J` 打开任务日志，确认任务状态与目标进度。
    在出货箱按 `E` 出货；在床边按 `E` 睡到次日 08:00，确认金币增加与自动存档。
11. 让林地史莱姆把生命打到 0，确认同样弹出 `YOU PASSED OUT`（玩家实体不会被
    当成敌人销毁）；也可走到荆棘验证持续扣血。死亡惩罚只结算一次。分别验证：
    - `RESPAWN AT BED` → 切回农场床边、生命/耐力回满、金币不再变化；
    - `LOAD AUTOSAVE` → 回到自动存档的地图/位置/背包/日期；
    - `MAIN MENU` → 回到主菜单。
12. 保存 → 改状态（击杀敌人/改变 AI 追击目标/推进任务）→ 主菜单 → 读档，确认
    地图/位置/背包/农田/日期/金币/掉落/任务进度/AI 状态/敌人存活与生命一致。
13. 退出游戏，重启 → 继续，确认回到存档状态。

**自动化覆盖（`--play-test`）**：体力配置 → 武器伤害 → 食物回复 → 对话/任务 → 种植 →
5 天生长 → 收获（任务完成与奖励）→ **任务日志** → 补种 → **背包分类** →
战斗/掉落 → **保存→改状态→读档（AI 状态/敌人生命/杂物/放置物/任务/生命）** →
8/7/1 小时睡眠档位 → 商店买卖 → **季节库存/价格波动** → 出货结算 → 农场门 →
**林地敌人追击/攻击/造成伤害** → 农场↔林地换图 →
**被敌人打死→GameOver→复活** → 荆棘死亡菜单/复活 → 保存/主菜单/读档（含林地 AI）→
存档槽往返 → 做汤/收取 → 死亡读档 → 死亡回主菜单。

**尚未自动覆盖**：真实音频素材、真实人工 30 分钟游玩、GitHub Actions 首次实跑。

## 7. 下一步计划（2026-10-27）

> 排序原则：先把“引擎已实现但 play 摸不到”的能力接出来，再补存档一致性，
> 最后做内容量与工程收尾。每项仍按“设计 → 实现 → 测试 → app 集成 →
> 三配置验证 → 回写文档”推进。

| 优先级 | 增量 | 目标 | 验收 |
|---|---|---|---|
| ✅ P0-1 | 叙事接入 play（已完成） | 农场 Elder 可交互：对话、Digit1–2 选择、Enter 继续；对话期间暂停玩法/时钟；任务接取/完成/奖励可见 | `--play-test` 已断言对话、接任务、收获完成、金币 100→110、种子 19→21；三配置通过 |
| ✅ P0-2 | 死亡菜单与读档（已完成） | 生命归零进入 GameOver 面板（复活 / 读自动存档 / 主菜单），金币惩罚只结算一次 | `--play-test` 已校验三条路径；三配置通过 |
| ✅ P0-3 | 剩余存档段（已完成） | 叙事（好感/任务/flags）、AI（agent 状态/目标）、世界对象（敌人/杂物/放置物）与玩家生命写入 `SaveFile` 并读回；新增段均为可选字段，旧档按空段加载，无需版本升级 | `--play-test` 已断言保存→改状态→读档后 AI/敌人/杂物/放置物/任务/生命逐项一致；三配置通过 |
| ✅ P1-1 | 战斗手感与敌人反击（已完成） | 林地敌人接入 M21 主动索敌/追击/攻击；受击闪烁 + 上浮伤害数字；被敌人打死进入 GameOver 且玩家不被销毁 | `--play-test` 已断言 chase/attack/hurt、combat death→respawn、林地 AI 存档；三配置通过 |
| ✅ P1-2 | 商店/任务/背包 UI（已完成） | 商店显示每日余量/季节可用性并按日确定性波动价格；`J` 任务日志；背包分类过滤 + 分页 + 滚轮 | `--play-test` 已断言季节库存、价格波动、任务日志与分类过滤；三配置通过 |
| ✅ P2-1 | 内容补齐（已完成） | 土豆/草莓、蝙蝠、12 物品、8 SFX + 1 音乐、22 张纹理；敌人 AI/纹理全部 JSON 驱动 | `--play-test` 断言 12/3/2/3 内容计数、音频目录与蝙蝠 spawn；三配置通过 |
| ✅ P2-1b | 攻击动画（已完成） | 玩家与林地敌人都用 M10 三帧挥砍，`hit` 帧事件生成 M20 命中盒；M21 新增 `setAttackHandler()` 让 AI 攻击先起手；无纹理时回落为即时命中 | `--play-test` 断言玩家与敌人都是“起手不掉血、命中帧掉血” |
| ✅ P2-2a | 工程收尾（已完成） | 存档 v1→v2 迁移、CI Debug/Release + 测试/frames/play-test/soak/1080p、格式化基线、`--soak`/`--resolution` | 三配置与 CI 命令本地全绿；`soak: 2 cycle(s), entities 11 -> 11, textures 22 -> 22 -> ok` |
| ✅ P2-2b | 压力与长时基线（已完成） | `--stress 1000` 精灵 + 20 idle agent；真实 30 分钟 `--soak 1800` | 本机 `p50 16.954 ms, p99 17.459 ms, avg 59.1 fps`；`183 cycle(s), entities 11 -> 11, textures 22 -> 22` |
| P2-2c | CI 首次实跑 | 推送后确认 Debug/Release 矩阵、smoke/soak/1080p 与 vcpkg 缓存在 Actions 上通过 | Actions 绿；失败日志可下载 |

**P0-1 已完成的落点**：

1. `processPlayerActions()` 的 `Interact` 分支识别 Elder（距离 32 px），调用
   `NarrativeService::startDialogue()` 并 `GameFlow::enterDialogue()`。
2. `onEvents` 路由 Digit1–2 / Enter / Escape 到 `chooseDialogue` / `advanceDialogue` /
   `cancelDialogue`；对话图结束时 `exitDialogue()`，不会卡在 Dialogue 状态。
3. `NarrativeService` 的背包指针在 play 中指向玩家快捷栏，任务奖励与 `HasItem`
   条件使用真实容器。
4. 收获与拾取会向 M23 报 `Collect` 事件；`QuestCompleted` 事件由 app 加金币并显示
   `QUEST COMPLETE`。
5. `--play-test` 已覆盖完整叙事闭环。

**下一步建议做 P2-2c（CI 首次实跑）**：推送后确认 Debug/Release 矩阵、smoke/soak/
1080p 与 vcpkg 缓存在 Actions 上通过；其余工程基线已完成。

**P2-2a 已完成的落点**：

1. `kSaveVersion` 升到 2；`save::registerDefaultMigrations()` 注册 v1→v2，
   只补空容器不覆盖值；app 启动时注册。单测 `Default migrations normalize v1 saves`
   与 `--play-test` 的 `save migration v1->v2 ok` 覆盖。
2. CI 改为 Debug/Release 矩阵：构建、`fa_tests`、`--frames 60`、`--play-test`、
   `--soak 20`、`--resolution 1920x1080 --frames 5`，失败上传日志。
3. 修复格式检查：改为 `scripts/check-format.py` 字节级比较（PowerShell `Out-String`
   会折行导致 283 个误报）；`.clang-format` 补 `SpacesBeforeTrailingComments: 2`、
   `IndentCaseLabels: true`，并一次性格式化全部 283 个源文件。
4. 新增 `--soak <秒>`：固定步模拟，每 600 步存档+读档+应用，结束时比对
   live entities 与纹理数；新增 `--resolution WxH` 覆盖窗口尺寸。
5. 实测：`soak: 2 cycle(s), entities 11 -> 11, textures 22 -> 22 -> ok`；
   `--resolution 1920x1080` 启动 smoke exit 0。
6. P2-2b 新增 `--stress N`：生成 N 个精灵 + 20 个 idle agent，记录 p50/p99 帧时间。
   本机 1000 精灵实测 `p50 16.954 ms, p99 17.459 ms, avg 59.1 fps`（600 帧）。
7. 真实 30 分钟 soak（`--soak 1800`，108000 帧）：
   `183 cycle(s), entities 11 -> 11, textures 22 -> 22 -> ok`。

**P2-1b 已完成的落点**：

1. `assets/textures/effects/slash.png`（48×16，3 帧）登记为 `tex.effect.slash`；catalog 驱动加载。
2. `DemoScene::buildAttackEffect()` 创建隐藏的挥砍实体，clip 为 0.07/0.07/0.10 秒，
   第 2 帧事件 `hit`。
3. `DemoScene::startAttack()` 播放动画并记录伤害/距离；`updateAnimation()` 每步
   让挥砍跟随玩家朝向，在 `hit` 事件里调用 `playerAttack()` 生成 M20 命中盒。
4. `handleAttack()` 改为调用 `startAttack()`；动画播放中再按右键返回
   `ATTACK BUSY`，动画结束才能再次挥砍。
5. 没有 `tex.effect.slash` 时自动回落到旧的即时命中，战斗不会因缺资源失效。
6. M21 新增 `AiService::setAttackHandler()`；`DemoScene` 为每个林地敌人创建红色
   挥砍精灵，AI 攻击先播放同一 3 帧 clip，`hit` 帧才 `spawnAttack`。农场训练
   agent 没有专属精灵，回落为即时攻击。
7. `--play-test` 断言 `attack animation start ok, wind-up ok, hit ok`、
   `forest attack animation ok` 与
   `enemy counterattack chase ok, attack ok, swing ok, wind-up ok, hurt ok`。

**P2-1 已完成的落点**：

1. `data/content.json` 新增 `item.potato*`、`item.strawberry*`、`item.slime_gel`、
   `item.bat_wing`（共 12 物品）、`crop.potato`（6 天）、`crop.strawberry`
   （8 天、`regrow_days=3`）、`enemy.bat` 与 `loot.bat`。
2. `data/entities.json` 敌人新增 `texture`/`tint`/`sight_radius`/`attack_range`/
   `move_speed`/`attack_damage`/`attack_cooldown`；林地新增 `forest_bat`。
3. app 不再硬编码两个敌人：按 spawn 顺序创建全部敌人，第 3 个起用 spawn `id`
   作为存档 role；AI 参数直接来自 spawn。
4. 纹理改为 catalog 驱动：app 遍历 `catalog.json` 加载全部 texture，场景按名称
   查表；新增贴图只需 PNG + catalog 条目。
5. `data/audio.json` 提供 8 个 SFX 与 1 首循环音乐；app 解析后按事件播放
   （锄地/浇水/播种/收获/拾取/命中/受伤/购买）。
6. `--play-test` 断言内容计数、音频目录、9 个 spawn 与蝙蝠参数。

**P1-2 已完成的落点**：

1. M24 `ShopEntry` 新增 `seasons` 与 `price_swing`，`ShopDatabase` 解析
   `"seasons": [...]` 与 `"price_swing"`；`buy` 在非当前季节返回 `kWrongSeason`。
2. `EconomyService::priceFactor()` 用 `(item, day)` 哈希算出 `[1-swing, 1+swing]`
   的确定性价格；app 在日推进/读档/每帧同步 `setSeason`/`setDay`。
3. 商店面板改为遍历库存条目：显示名称、今日价格、`STOCK n` 或 `OUT OF SEASON`。
4. M23 新增 `questStates()`；`J` 打开任务日志，按“进行中 → 已完成”排序并显示
   目标进度（如 `Collect Turnip 1/1`）。
5. 背包面板加分类按钮、`PREV/NEXT` 分页与滚轮翻页；分类逻辑由
   `itemMatchesInventoryFilter()` 统一，`--play-test` 直接覆盖 seed/weapon 判定。

**P1-1 已完成的落点**：

1. 林地 `forest_slime_a/b` 在 play 模式挂 `AiAgent`，玩家在林地挂 `AiTargetTag`；
   农场训练假人仍保持静止。
2. `processCombatEvents()` 区分玩家/敌人受击：玩家掉血显示 `HURT`，敌人显示 `HIT`；
   玩家生命归零走 `handlePassOut("combat")`，不再被 M20 的死亡事件销毁。
3. 受击实体短暂变红（`HitFlash`），并在世界坐标上浮一组 `-N` 伤害数字。
4. `world.ai` 改为按 role 的数组，农场 agent 与林地两个敌人的状态/计时器/目标
   都可存档；旧单对象格式仍可读取。

## 8. 开放问题

- 切片是否需要战斗？**已决定：保留**，M20/M21 已实现并跑通；林地敌人已会反击，
  攻击动画仍待补。
- 瓦片尺寸与角色比例：**已定** 16×16、640×360、脚部盒 10×6（ADR-0003）。
- 死亡菜单已实现「复活 / 读自动存档 / 主菜单」；金币惩罚在死亡时先扣一次，
  读档会回到存档中的金币。是否需要在读档路径上保留惩罚，仍需产品确认。
- 叙事存档粒度：**已定** 存好感/flags/每日上限/任务进度，不存对话节点历史；
  读档会结束当前对话，玩家重新与 NPC 交谈。
- 内容优先级：**已定** 3 作物/2 敌人/12 物品/8 SFX 均已补齐；真实美术与音频素材
  仍是后续替换项（当前是占位 PNG 与程序化音）。
- 加工已接入商店面板；是否继续做设备实体、燃料与队列上限（P0-3 存档段已完成）。
- 音效/音乐素材来源（自制/购买/免费）仍未定；当前是代码生成的占位 PCM。
- 性能目标硬件与分辨率（1280×720 / 1920×1080 / 60 FPS）：两种分辨率已有启动
  smoke；本机 1000 精灵基线为 p50 16.95 ms / 59.1 fps，尚未在目标集成显卡上复测。
