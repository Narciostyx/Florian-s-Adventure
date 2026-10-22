# 计划完成情况

**这份文档是「继续任务」的唯一入口。** 新开一个对话继续开发时，先读这里，再读对应的模块文档与构建记录，不需要重新翻历史。

> 需要逐项完善程序时，先看 [已知限制与后续改进汇总](known-limitations.md)：它把各模块的限制和跨模块风险集中成一份清单。
>
> 需要定位代码时看 [代码速查](code-reference.md)：按模块列出命名空间、依赖、公开头文件、类型、方法、枚举与常量。

- 最后更新：独立第二张地图 + `SceneStack::replace` + 地图 JSON 外置 + 垂直切片重新盘点（三配置复测）
- 下一步：**按 §6 先把 M23 叙事接入 play，再做死亡菜单/读档、剩余存档段与战斗打磨**
- 当前总体：**28 个模块全部已实现**（M00–M27）
- 当前可运行内容：窗口 + 主循环 + 事件/作业 + 输入 + 渲染 + 资源缓存 + **ECS 世界与场景栈** + **AABB 碰撞与查询** + **瓦片地图与图层提取** + **交互目标选择与格子放置** + **玩家移动/朝向/耐力/闪避** + **带引用校验的内容数据库** + **内置位图字体与即时模式 HUD/控件** + **物品堆叠/背包/快捷栏/装备/使用分发** + **PCM16 软件混音与总线音量** + **调试控制台/统计覆盖层/日志窗口/实体检查器/物理调试** + **JSON 存档槽/原子写/`.bak` 恢复/迁移** + **帧动画/循环/事件/四向状态机** + **伤害/攻击窗口/投射物/状态/死亡事件**；画面是**全部色块占位**，scripted 用 40×23 地图（920 格、143 实心、16 可耕地），play 用两张独立的 40×23 地图（农场/林地），标记由 M16 以八向朝向驱动、M13 求解并停在 x=344，M17 在标记旁选目标、调用一次工具并占一个放置格，M15 加载物品/作物/配方/敌人/掉落表，M18 堆叠 120 个种子、转移 21 个到快捷栏、装备锄头并使用一份汤，M09 每帧绘制 HUD、耐力条、按钮、复选框、滑条与热键栏文本，M08 生成 440 Hz/220 Hz 占位 PCM、播放 one-shot 与循环、设置总线音量并在 2 帧后淡出 one-shot，M27 在 Debug/Development 提供 F3 统计、F4 日志、F5 物理调试与控制台（`teleport`/`give`/`entities`/`save`/`load`/`slots`），M14 每轮 smoke 写 `saves/autosave.json` 并读回版本/场景/实体/标记/耐力摘要，M10 在固定步推进一个 4 帧草地动画并在 frame 0/2 触发 `step` 事件，M20 用两个 dummy 跑通两次近战、投射物与中毒并击杀一个 dummy，M19 在 (4,8) 跑通锄地 → 播种 → 浇水 → 5 天成熟 → 收获，并在 (7,11) 温室与 (6,9) 室外验证冬季规则，M21 用一个紫色 agent 追击静止假人、攻击一次并在丢失目标后返回出生点，M22 用 0.01 s/game-minute 的快速时钟跑出 08:00 → 09:40、计算环境光并在 Weather 层绘制暗化层与两个占位点光，M23 用一个 NPC 跑通对话分支、接任务、收集完成、送礼与每日上限，并在底部对话框留下可交互选项，M24 用玩家背包跑通买种子、卖萝卜、做汤、出货与次日结算（金币显示在 HUD），M25 用脚本跑通新游戏 → 暂停 → 保存 → 日终 → 死亡 → 复活 → 主菜单 → 读档 → 退出（交互式主菜单/暂停/存档槽由 `--play` 提供），M26 在 Debug/Development 提供 F2 瓦片编辑器、撤销/重做与 JSON 保存/加载（Release 不链接）；`--play` 启动进入**主菜单**（新游戏/继续/存档槽/退出），`Esc` 暂停（继续/保存/存档槽/主菜单）；快捷栏 1–3 = 锄头/水壶/种子，左键使用，指向成熟作物自动收获；右键近战，敌人死亡掉落并自动拾取；床边 `E` 任意时间睡觉，醒来固定次日 08:00，睡满 8 h 回满体力、不足按 `(小时/8)²` 递减至最低 1%；橙色出货箱旁 `E` 把作物加入待结算清单并在次日换成金币，紫色商人旁 `E` 打开商店买种子/卖萝卜；`I` 打开背包面板，点击 MOVE 在背包/快捷栏之间移动物品；HUD 同时显示生命与耐力；启动从 `data/*.json` 读取内容/商店/叙事/资源目录与实体生成表，地图布局从 `data/maps/*.json` 读取（`farm.json` / `farm-scripted.json` / `forest.json`），`config/game.json` 控制窗口/UI/玩法参数（缺失回落默认），纹理优先 `assets/*.png`、缺图回落程序占位，面板居中且字号 1.25×，窗口最小 1280×720 可放大；play 模式使用两张独立 40×23 地图（农场/林地），门口 `portalAt` 触发 `SceneStack::replace` 换场景，农田/背包/叙事等跨场景状态由 `GameState` 持有；林地有额外 dummy 与 3 格荆棘（每 0.7s 扣 5 血），HUD 显示 `FARM`/`FOREST`；生命归零会床边复活并扣 10% 金币；当前地图、地面掉落物、金币、出货清单、加工任务与每日商店余量随存档写入并读回；`--play-test` 无头跑通耕地 → 播种 → 浇水 → 5 天 → 收获 → 击杀 dummy → 拾取掉落 → 8h/7h/1h 睡眠档位 → 商店买卖 → 出货结算 → 保存/主菜单/存档槽/读档往返（位置、耐力、背包、作物、日期、金币、当前地图一致）并返回失败码，但还没有完整玩法

## 0. 上下文快满时的交接（强制）

当当前会话的上下文接近上限时，**立即停止继续生成代码或文档**，不要开启新模块、
大块重构或新的三配置验证。先按下面清单把交接信息写进本文件，再结束本轮：

1. **当前进度**：正在做的模块、已完成/进行中/未开始，最后修改的文件。
2. **未完成项**：还差哪些实现、测试、app 集成、三配置验证或文档回写。
3. **下一步**：建议的模块与第一步动作；若已选定，写出明确的续做顺序。
4. **验证状态**：最后通过的配置与命令，以及任何已知失败/警告。
5. **未提交改动**：列出需要保留的工作树文件；不要为了“收尾”擅自提交或删除。
6. **恢复入口**：指出下轮应重读的 `docs/build/mXX-*.md` 与本节。

> 目标：下轮会话只读本文件与对应模块/构建文档就能继续，不依赖上一轮的聊天记录。

## 1. 长期有效的规约

这几条来自用户明确要求，**任何后续改动都必须遵守**：

| 规约 | 说明 |
|---|---|
| 源码只写 Doxygen 注释 | `///`、`//!`、`\brief`、`\param`；唯一例外是 `}  // namespace fa::core` 这类工具锚点。踩坑、背景、权衡**一律写进 `docs/build/mXX-*.md`**，不写进源码 |
| 美术一律色块占位 | 不引入真实图片；纹理由 catalog 的 `color`/`altColor`/`tile` 程序化生成。见 [美术资源策略](architecture/art-policy.md) |
| 文档必须同步 | 每次交付同时更新：模块文档（`docs/modules/`）、构建记录（`docs/build/`）、本文件、[索引](index.md) |
| 三个配置都要过 | Debug / Development / Release × x64，构建零 warning（`/W4 /WX`），`fa_tests.exe` 全绿，`fa_app.exe --frames 60` exit 0 |
| 不引入新依赖除非必要 | M06 自研 GL 函数表而不引 glad；M05 用反向接口而不让 `fa_assets` 依赖 `fa_render` |
| 上下文将满时 | 立即停止继续生成，先按 §0 写交接；不要开启新模块或大重构 |

## 2. 环境事实

| 项 | 值 |
|---|---|
| Visual Studio | Community 2026，MSVC `14.51.36231`（工具集 `v145`） |
| MSBuild | `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe` |
| vcpkg | `D:\CPPLibraries\vcpkg\vcpkg.exe`，命令行**每次都要显式** `$env:VCPKG_ROOT`；VS 里需要用户级 `VCPKG_ROOT` 或 `vcpkg integrate install` |
| triplet | `x64-windows-static-md` |
| 解决方案文件 | `FloriansAdventure.sln`（**没有下划线**）；根目录的 `Florian_Adventure.*` 是 VS 误生成、无源文件也无依赖的空工程，已删除 |
| VS 直接生成 | 选 `x64` + Debug/Development/Release；依赖由各 `.vcxproj` 的 `ProjectReference` 声明，不需要手工加 |
| 全解决方案构建 | `MSBuild.exe FloriansAdventure.sln /p:Configuration=X /p:Platform=x64 /m /nologo /v:m` |
| 单工程构建 | 必须带 `/p:SolutionDir="$PWD\"` |
| 二进制 | `build\bin\x64\<Config>\fa_tests.exe`、`fa_app.exe` |
| PowerShell 调可执行文件 | 必须用 `& ".\build\bin\x64\$cfg\fa_tests.exe"`，直接写路径会被当字符串 |

全局编译开关：`/W4 /WX`、`stdcpp20`、`ConformanceMode`、`SDLCheck`、`ExceptionHandling=false`、
`RuntimeTypeInfo=false`、`/utf-8 /Zc:__cplusplus /Zc:preprocessor`。

需要 `/EHsc` 的工程：`fa_platform`（文件系统）、`fa_events`（`<chrono>`）、`fa_input`（键位 JSON）、
`fa_assets`（catalog JSON）、`fa_tilemap`（地图 JSON）、`fa_player`（输入头间接引入 `<unordered_map>`）、
`fa_content`（内容 JSON）、`fa_tests`（Catch2）、`fa_app`（把上面全 include 了一遍）。
判据不是「模块高不高级」，而是 **include 图里有没有 `<chrono>`、nlohmann/json 或 `<unordered_map>`**。
`fa_render` 与 `fa_scene` 都不需要——`fa_scene` 只依赖 `fa/core`、`fa/time`、`fa/render` 的头。
`fa_physics` 与 `fa_player` 需要它是因为 `<unordered_map>`；`fa_tilemap` 与 `fa_content` 需要它是因为 nlohmann/json。

## 3. 模块状态

### 已实现（28 个）

| 模块 | 名称 | 用例（新增） | 关键交付 | 文档 |
|---|---|---|---|---|
| M00 | Build & Dependency Infrastructure | — | `.sln` + 27 个引擎 `.vcxproj` + app/tests、`vcpkg.json`、`Directory.Build.props`、`scripts/build.ps1`、CI | [设计](modules/00-build-infrastructure.md) · [实测](build/m00-skeleton.md) · [踩坑](build/m00-fixups.md) |
| M01 | Core Foundation | 22 | `Result`/`Status`/`Error`、`Vector2`、`Rect`、`Rng`、断言与日志 | [设计](modules/01-core-foundation.md) |
| M02 | Platform Layer | 8 | 窗口、事件泵、`IFileSystem`（含原子写）、单调时钟、消息框、`IGlContext` | [设计](modules/02-platform-layer.md) · [实测](build/m02-platform.md) |
| M03 | Events & Jobs | 26 | `EventBus` + RAII `Subscription`、`JobSystem` 线程池与 `parallelFor` | [设计](modules/03-events-and-jobs.md) · [实测](build/m03-events-jobs.md) |
| M04 | Time & Frame Loop | 20 | 固定步时钟、`FrameContext`、时间域暂停/缩放、`FrameLoop` | [设计](modules/04-time-and-frame-loop.md) · [实测](build/m04-frame-loop.md) |
| M05 | Asset Pipeline | 61 | `AssetId`/`AssetHandle`、`AssetCatalog`、异步加载 + 帧线程上传、占位降级、`ITextureUploader` 反向桥；app 注册 stb_image 解码 `assets/*.png` | [设计](modules/05-asset-pipeline.md) · [实测](build/m05-assets.md) |
| M06 | Rendering | 42 | `IRenderer` + `RenderQueue` + `Camera2D`（边界夹取 + 死区/平滑/限速跟随）+ `Viewport`、自研 42 项 GL 函数表、精灵批处理、调试绘制 | [设计](modules/06-rendering.md) · [实测](build/m06-render.md) |
| M07 | Input | 36 | `ActionId` + `Binding` + 每上下文 `ActionMap`、上下文栈、`IInput` 窄接口、JSON 键位持久化 | [设计](modules/07-input.md) · [实测](build/m07-input.md) |
| M08 | Audio | 12 | `AudioClip` PCM16、32 voice 软件混音、Master/SFX/Music/Ambience 总线、pan/loop/fade/pitch、距离衰减、无锁 SPSC 命令队列、SDL3 `IAudioDevice` 回调、原子统计 | [设计](modules/08-audio.md) · [实测](build/m08-audio.md) · [ADR](adr/0005-audio-backend.md) |
| M09 | UI & Text | 16 | 内置 3×5 ASCII 位图字体、字形回退、测量/换行/对齐、投影/描边、立即模式面板/按钮/复选框/滑条/进度条、`RenderLayer::UI` 与主题 | [设计](modules/09-ui-and-text.md) · [实测](build/m09-ui-and-text.md) |
| M10 | Animation | 4 | `AnimationClip` 帧/时长/事件、`Animator` 纯数据、`AnimationService` 循环/完成/跨帧事件、四向 `Direction`、`AnimationStateMachine` 条件转换与方向变体 | [设计](modules/10-animation.md) · [实测](build/m10-animation.md) |
| M11 | Scene / World & ECS | 78 | `World` + `EntityId` 代际句柄 + `SparseSet` + `CommandBuffer`、`SystemPipeline` 三钩子调度、`SceneStack` 栈式场景、`SpriteExtractor` 纯数据提取；app 用 `replace()` 在农场/林地之间换场景 | [设计](modules/11-scene-world-ecs.md) · [实测](build/m11-scene-ecs.md) |
| M12 | Tilemap & World | 17 | `Tilemap` + `TileId`/`TileCoord`/`TileBounds` + 多图层 + 瓦片属性、`worldToGrid`/`gridToWorld`/`queryArea`/`portalAt`、JSON 地图加载、4 邻接自动瓦片、`extractTiles` 视锥裁剪、`TilemapSolids` 物理适配器；app 从 `data/maps/*.json` 载入农场/林地，缺文件回落内置布局 | [设计](modules/12-tilemap-and-world.md) · [实测](build/m12-tilemap.md) |
| M13 | Physics & Collision | 40 | `Collider` + `CollisionLayer` 双向掩码、`SpatialHash` 宽相、`TileGrid` 静态碰撞、轴分离 + ≤4px 子步求解、`overlap`/`overlapCircle`/`raycast`、触发器进出事件 | [设计](modules/13-physics-collision.md) · [实测](build/m13-physics-collision.md) |
| M14 | Serialization & Save | 7 | `SaveArchive` JSON 值树（隐藏 nlohmann）、`SaveMetadata`/`SaveContext`/`SaveFile`、`SaveService` 多槽/原子写/`.bak` 恢复/迁移链、`ComponentSerializerRegistry` | [设计](modules/14-serialization-save.md) · [实测](build/m14-serialization-save.md) |
| M15 | Content Database | 11 | `ContentId` + `ItemDef`/`CropDef`/`RecipeDef`/`EnemyDef`/`LootTableDef`、可选 `ItemDef::toolKind`、季节掩码、JSON 多文档合并、前缀/重复/必填/数值校验、四类引用完整性、全量错误收集、按 ID/标签/类别查询 | [设计](modules/15-content-database.md) · [实测](build/m15-content-database.md) |
| M16 | Player Controller | 14 | `PlayerController` 走/跑/加速度/斜向归一化、**八向朝向**（45° 扇区 + 斜向折叠）、状态机与动作锁、耐力/闪避/无敌窗口、`PlayerSystem` 输入适配、`PlayerActionRequest` 记录、动画参数 | [设计](modules/16-player-controller.md) · [实测](build/m16-player-controller.md) |
| M17 | Interaction & Grid Placement | 15 | `InteractionService` 目标选择（实体优先/朝向/距离/视线）、`Interactable` 组件、`IToolHandler` 分发、`PlacementGrid` 多格占用、`PlacementPreview`、交互/放置事件记录 | [设计](modules/17-interaction-grid-placement.md) · [实测](build/m17-interaction-grid.md) |
| M18 | Inventory & Items | 22 | `ItemStack` + 内容驱动堆叠、`Inventory` 固定槽位与原子移除/交换/拆分/合并/转移、`Hotbar` 选中与消耗、`Equipment` Tool/Weapon、`IItemUseHandler` 分发、`PlayerInventory` 快速转移 | [设计](modules/18-inventory-items.md) · [实测](build/m18-inventory-items.md) |
| M19 | Farming & World Objects | 14 | `FarmTile`/`CropState`、无 ECS 的按格农田、锄地/浇水/播种/按天生长/收获、季节与温室、再生作物、`FarmingDayContext` 消费 M22 天气、`FarmingToolHandler`（Hoe/WateringCan/Seed/Harvest/Axe/Pickaxe，可 `setInventory` 切到玩家快捷栏）、`WorldObjectLayer`/`DebrisLayer`、`entries()`/`restore()` 整块农田快照 | [设计](modules/19-farming-world-objects.md) · [实测](build/m19-farming-world-objects.md) |
| M20 | Combat | 7 | `Damage`/`Health` 结算、M16 闪避无敌、攻击窗口与单实例去重、投射物 raycast/move、击退、Poison/Burn/Slow、四类事件与 `CombatStats`；play 模式右键攻击、死亡掉落与自动拾取 | [设计](modules/20-combat.md) · [实测](build/m20-combat.md) |
| M21 | AI | 11 | `AiService` + `AiAgent`/`AiTargetTag`/`BehaviorProfile`、A*（4 邻接 + 视线平滑）、Bresenham 视线、感知节流、Idle/Patrol/Chase/Attack/Flee/Return、M13 移动、M20 攻击执行、`AiStats` | [设计](modules/21-ai.md) · [实测](build/m21-ai.md) |
| M22 | Calendar / Weather / Lighting | 22 | `Season`/`Weather`/`GameDate`/`WorldClock`、0–24 时刻与跨日/季/年、`HourChanged`/`DayChanged`/`SeasonChanged`/`WeatherChanged`、按季节权重随机天气与 RNG 状态保存、自动浇水/生长倍率、环境光曲线与室内外、`LightSet` 点光与线性衰减采样 | [设计](modules/22-calendar-weather-lighting.md) · [实测](build/m22-calendar-weather-lighting.md) |
| M23 | NPC / Dialogue / Quests | 10 | `NarrativeDatabase` + `NarrativeService`、NPC/对话图/任务 JSON、白名单条件与动作、每日好感/送礼、事件驱动任务进度与物品奖励、底部对话框与 Digit1–3 选择 | [设计](modules/23-npc-dialogue-quests.md) · [实测](build/m23-npc-dialogue-quests.md) |
| M24 | Economy / Crafting / Shops | 11 | `EconomyService` + `ShopDatabase`、钱包、买卖原子性、每日限购、M15 配方加工任务、出货箱次日结算、`restoreShipping`/`restoreJobs`/`restoreRemaining`、经济事件/统计；app 橙色出货箱 + 紫色商人商店 + 经济段存档 | [设计](modules/24-economy-crafting-shops.md) · [实测](build/m24-economy-crafting-shops.md) |
| M25 | Game Flow | 10 | `GameFlow`/`IGameFlow`、`AppState` 状态机、新游戏/读档、暂停/对话、日终结算（`EndOfDayHandler`）、自动存档、`saveToSlot(slot)`、睡眠恢复规则（`Sleep.hpp`）、死亡/复活、退出、7400 错误段；app 主菜单/暂停/存档槽 + 床边 `E` 任意时间睡觉 + 门口换地图；`GameState` 持有跨场景农田/背包/叙事/玩家状态 | [设计](modules/25-game-flow.md) · [实测](build/m25-game-flow.md) |
| M26 | Editor Tools | 10 | `MapEditor` + `EditorHistory` + `EditorOverlay`、Brush/Rect/Fill/Erase/Picker、命令差分 undo/redo、M12 `saveTilemapToJson()`、M09 状态面板、F2/1–5/`[`/`]`/Z/Y/S/L 快捷键；Release 不链接 | [设计](modules/26-editor-tools.md) · [实测](build/m26-editor-tools.md) |
| M27 | Debug Tools | 11 | `DebugConsole` 命令/引号/历史、`CVarRegistry` 四种类型与 save/load、`DebugLogSink` 环形日志、`DebugStats`（含角色相机与 AI 锁定目标）、实体快照、`UiContext` 覆盖层、M13 `DebugShape` 适配、F3/F4/F5/控制台事件路由；Release 不链接 | [设计](modules/27-debug-tools.md) · [实测](build/m27-debug-tools.md) |

实际落地顺序是 M01 → M02 → M04 → M03 → M07 → M06 → M05 → M11 → M13 → M12 → M17 → M16 → M15 → M09 → M18 → M08 → M27 → M14 → M10 → M20 → M22 → M19 → M21 → M26 → M23 → M24 → M25，所以上表顺序与时间顺序不同。
M12 排在 M13 之后，是因为它要适配 M13 的 `ISolidTiles`；M17 排在 M12 之后，因为它要用地图做目标与放置校验；
M16 把输入、物理和 M17 的交互请求串起来；M15 是 P0，后续 M18/M19/M24 都靠它；
M09 补上了此前「任何文字都画不出来」的空白；M18 接着把 M15 的物品定义变成可堆叠、可转移、可使用的容器状态，
并在 HUD 里显示选中的快捷栏物品；M08 最后补上音频，用 SDL3 设备回调 + 自研软件混音验证
one-shot、循环、总线音量与淡出，不引入 SDL_mixer。

M22 保持更窄的依赖：`fa_world` **只引用 `fa_core`**。日历/天气是纯逻辑，事件是内部
`vector`，光照是纯数据；组装根负责把 `advance()` 接到帧循环、把事件转发给玩法模块、
在 Weather 层画暗化层与点光，因此 `fa_world` **不需要 `/EHsc`**（见
[构建记录](build/m22-calendar-weather-lighting.md) §1、§3）。

M08 保持窄依赖：`fa_audio` 只引用 `fa_core` 与 `fa_platform`，**不引用 `fa_assets`、`fa_render`、`fa_scene`**，
`AudioClip` 由 M08 自己定义；命令队列是唯一的跨线程入口，统计通过原子快照发布，所以 `fa_audio` **不需要 `/EHsc`**
（见 [构建记录](build/m08-audio.md) §3 第 8 条）。

M27 是 L4 工具，按设计读取所有下层，但**不反向依赖任何玩法模块**：命令由 app 注册，
`DebugStats` 由 app 填充，实体检查器只做值快照。`fa_debug` 因为 include `fa/scene/World.hpp`
（`<unordered_map>`）需要 `/EHsc`；`fa_app` 只在 `Configuration != Release` 时引用它，
Release 可执行文件不含调试代码（见 [构建记录](build/m27-debug-tools.md) §3 第 4、6 条）。

M14 只引用 `fa_core`、`fa_platform`、`fa_scene`，**不依赖 M05/M06/M09/M12/M15**；
`SaveArchive` 用 PIMPL 把 `nlohmann::json` 关在 cpp 里，公共头不泄漏第三方类型，
`fa_save` 因此单独打开 `/EHsc`。玩法组件通过 `IComponentSerializer` 注册，不反向依赖存档实现
（见 [构建记录](build/m14-serialization-save.md) §3 第 6 条）。

M10 只引用 `fa_core` 与 `fa_render`，**不依赖 M05/M11**：`AnimationClip` 只存裸
`render::TextureHandle`，`Animator` 是纯数据，ECS 调度由 composition root 负责；
没有 JSON/`<chrono>`/`<unordered_map>`，所以 `fa_anim` 不需要 `/EHsc`
（见 [构建记录](build/m10-animation.md) §3 第 6 条）。

M20 只引用 `fa_core`、`fa_scene`、`fa_physics`、`fa_player`，**不依赖 M15/M18**：
掉落表以字符串 id 事件交给组装根，M20 不 include 内容库/背包；伤害/攻击/投射物/状态
都只读写 ECS 组件，表现由 app/M06/M10 消费事件（见 [构建记录](build/m20-combat.md) §3 第 9 条）。

M05 交付时还顺带清理了一处历史遗留：`fa_render.vcxproj` 里有一条指向 `fa_assets` 的 `ProjectReference`，但没有任何源文件用它，已删除。**M05 与 M06 现在编译期互不依赖**——上传走 `ITextureUploader` 反向接口，由 `fa_app` 适配。

M11 同样保持窄依赖：`fa_scene` 只引用 `fa_core`、`fa_time`、`fa_render`，**不引用 `fa_assets`**。
`Sprite` 只存裸 `render::TextureHandle`，纹理保活归加载方（asset manager 的生命周期长于任何场景），
所以使用 ECS 的模块不会被资源系统的 JSON 依赖与 `/EHsc` 连坐。提取接口产出纯数据 `RenderQueue`
而不是接收 `IRenderer&`，因此渲染提取的测试完全不需要 GL 上下文。

M13 继续保持窄依赖：`fa_physics` 只引用 `fa_core`、`fa_time`、`fa_scene`，**不引用 `fa_assets`、`fa_events`、`fa_render`**。
静态地形通过单方法接口 `ISolidTiles` 借用，M12 已用 `TilemapSolids` 适配器实现它，所以物理仍然不会反向依赖地图模块；
调试绘制产出 `DebugShape` 列表而不是接收 `IRenderer&`；触发器结果按步记录在 `triggerEvents()` 里，由组装根决定是否发布到事件总线。
代价是 `fa_physics` 因为 `std::unordered_map` 需要 `/EHsc`（见 [构建记录](build/m13-physics-collision.md) §5 第 1 条）。

M12 也保持窄依赖：`fa_tilemap` 只引用 `fa_core`、`fa_time`、`fa_render`、`fa_scene`、`fa_physics`，**不引用 `fa_assets`**。
`TileDef` 只存裸 `render::TextureHandle`，JSON 里的 `texture` 是资源名，由 `fa_app` 解析后调用 `setTileTexture()` 绑定；
`Tilemap` 是可拷贝/可移动的值类型，物理桥是一个单独的非 owning `TilemapSolids`，因此 `core::Result<Tilemap>` 能按值返回。
提取接口同样是纯数据 `RenderQueue`；代价是 `fa_tilemap` 因为 nlohmann/json 需要 `/EHsc`（见 [构建记录](build/m12-tilemap.md) §5 第 4 条）。

M17 继续窄依赖：`fa_interaction` 只引用 `fa_core`、`fa_scene`、`fa_physics`、`fa_tilemap`，**不引用 `fa_assets`、`fa_events`、`fa_input`、`fa_content`**。
工具行为通过 `IToolHandler` 注册，具体内容 ID 暂用不透明 `std::uint32_t payload`（M15 已实现，接线时替换）；
交互/放置结果按调用记录在 `events()`，由组装根决定是否发布到 M03；放置预览是纯数据 `PlacementPreview`，
因此 M17 不需要 `IRenderer&`，也不需要 `/EHsc`（见 [构建记录](build/m17-interaction-grid.md) §5 第 11 条）。

M16 只引用 `fa_core`、`fa_input`、`fa_time`、`fa_scene`、`fa_physics`，**不引用 `fa_tilemap`、`fa_interaction`、`fa_content`、`fa_assets`**。
控制器只写 `physics::Velocity`，位置仍由 M13 写；工具/攻击/交互/背包都记录成 `PlayerActionRequest`，由组装根决定转发；
具体工具效果和伤害留给 M19/M20。`PlayerSystem.cpp` 因为 include `fa/input/Input.hpp`（间接 `<unordered_map>`）需要 `/EHsc`
（见 [构建记录](build/m16-player-controller.md) §5 第 11 条）。

M15 只引用 `fa_core` 与 `fa_assets`，**不引用 M11/M12/M16/M17**：定义只存 ID 与值，不含实体指针或瓦片结构；
文件读取归 M05/composition root，`loadDocument()` 只接收文本。它用 nlohmann/json 做解析，
所以 `fa_content` 单独打开 `/EHsc`（见 [构建记录](build/m15-content-database.md) §5 第 11 条）。

M09 只引用 `fa_core` 与 `fa_render`，**不引用 `fa_assets`、`fa_input`、`fa_scene`、`fa_events`**：
字体图集与 1×1 白纹理以裸 `TextureHandle` 传入，由 app 上传；鼠标快照与控件状态都由调用方每帧提供；
文字与四边形都产出纯数据 `DrawItem`，所以 `fa_ui` 不需要 GL、资源或输入模块，也**不需要 `/EHsc`**
（见 [构建记录](build/m09-ui-and-text.md) §5 第 10 条）。

M18 只引用 `fa_core` 与 `fa_content`，**不引用 `fa_assets`、`fa_render`、`fa_input`、`fa_interaction`**；
只 include `fa/scene/EntityId.hpp`（纯值头），不链接 `fa_scene`。物品只存 `ContentId`/数量/耐久，
不存 `ItemDef*`；`ContentDatabase` 是借用指针，生命周期由组装根保证。handler 注册表用 `std::vector`
线性查找，所以 `fa_inventory` 也**不需要 `/EHsc`**（见 [构建记录](build/m18-inventory-items.md) §5 第 10 条）。

**验收快照**（测试三配置一致；冒烟三配置都是 exit 0。固定步数由墙钟累积，并行负载下可能明显多于 60，不要逐字比对帧数或 y 坐标；x 坐标才是稳定断言）：

```text
fa_tests.exe → All tests passed (30416 assertions in 554 test cases)
             "[anim]"     → 59 assertions in 4 test cases
             "[audio]"    → 9808 assertions in 12 test cases
             "[combat]"   → 50 assertions in 7 test cases
             "[debug]"    → 122 assertions in 12 test cases
             "[save]"     → 82 assertions in 7 test cases
             "[inventory]" → 288 assertions in 22 test cases
             "[ui]"       → 111 assertions in 16 test cases
             "[content]"  → 181 assertions in 11 test cases
             "[player]"   → 140 assertions in 15 test cases
             "[render]"   → 495 assertions in 47 test cases
             "[camera]"   → 313 assertions in 5 test cases（含在 [render] 内）
             "[world]"    → 122 assertions in 22 test cases
             "[farming]"  → 238 assertions in 14 test cases
             "[ai]"       → 101 assertions in 11 test cases
             "[editor]"   → 310 assertions in 10 test cases
             "[narrative]" → 74 assertions in 10 test cases
             "[economy]"  → 105 assertions in 11 test cases
             "[flow]"     → 160 assertions in 10 test cases
fa_app.exe --frames 60 (Debug/Development)
             → m08: 1 voice(s), 2 played, 1 stopped, 0 dropped, 48000 Hz, 2 channel(s)
             → m10: 60 update(s), frame 1/4, 5 event(s), last step@0, looping
             → m14: slot autosave, v1, scene demo, day 1, 1.00s, 10 entities,
                    marker (344, 242.792), stamina 100.0, 2 slot(s), backup no
             → m06 camera: (320.0, 188.0), follow deadzone 48.0x32.0, 10.0/s, cap 0.0
             → m16: move, facing south-east, speed 90.0, stamina 100.0,
                    60 update(s), 60 move / 0 idle, 0 dodge(s), 0 action(s)
             → m19: 3 tilled, 2 crop(s), 0 harvestable, 1 dead;
                    3 till(s)/7 water(s)/3 plant(s)/1 harvest(s), 1 item(s),
                    stage 3 (ready), greenhouse ok, winter wilt ok, seed 2, 19 event(s)
             → m20: 3 attack(s), 5 hit(s), 51 damage, 1 kill(s),
                    1 projectile(s)/1 hit(s)/0 wall hit(s), 1 status tick(s), 10 event(s)
             → m21: attack, target valid (visible), 1 path node(s), 1 agent(s),
                    9–10 perception(s), 9–10 target(s), 2 path(s), 38 move(s),
                    1 attack(s), 4 state change(s)
             → m22: year 1 spring day 1 09:39–09:40, sunny -> sunny, ambient 1.00,
                    auto-water no, growth x1.00, 1 day(s)/2 hour(s)/0 season(s),
                    1 weather change(s), 1 event(s)
             → m23: active, node greet, 2 choice(s), affinity 4, quest completed,
                    seed 4, 2 dialogue(s) [1 ended], 4 node(s), 1 choice(s),
                    1 gift(s), 12 event(s)
             → m24: gold 170 (+60 settled), seed 102, turnip 1, soup 2,
                    buy/sell/craft/collect/ship ok; 1 bought / 1 sold / 1 crafted /
                    1 shipped, 30 spent / 100 earned, 8 event(s)
             → m25: quit, slot slot1, day 1, 1.50s, dirty no;
                    1 new / 1 load(s) (0 failed) / 1 save(s) / 2 autosave(s),
                    1 day end(s), 1 pause(s)/1 resume(s), 1 death(s)/1 respawn(s),
                    1 menu return(s), 1 quit(s), 10 transition(s), 20 event(s);
                    new ok, pause ok, save ok, day ok, over ok, respawn ok,
                    menu ok, load ok, quit ok
             → m27: 28 console command(s), 0 cvar(s), camera ok, 1 ai target(s),
                    F3 stats / F4 log / ` console
             → m26: off, tool brush, layer 0, brush 0, undo 0 / redo 0,
                    0 command(s), dirty no, map 40x23, save ok, load ok
fa_app.exe --play-test
             → play-test: till ok / plant ok / water ok / harvest ok / replant ok
             → play-test: turnips 0 -> 1
             → play-test: combat killed, seeds 0 -> 2
             → play-test: bed near
             → play-test: sleep wake 08:00, 8h 100.0, 7h 76.6, 1h 1.6 -> ok
             → play-test: shop gold 161, shipped 3, settled +51 -> ok
             → play-test: props shipping ok, merchant ok
             → play-test: portal farm->forest ok, forest->farm ok, forest dummy ok
             → play-test: inventory move ok, panel ok/ok, health 100
             → play-test: thorns death ok, gold 161 -> 145
             → play-test: save ok / menu ok / load ok
             → play-test: load marker ok, bag ok, day ok, farm ok, map ok, gold ok, loot ok
             → play-test: menu paused ok, slot listed ok, menu ok, resumed ok
             → play-test: OK
             → stack: 1 push(es), 0 pop(s), 5 replace(s), 0 pause(s), 0 resume(s)
             → M02+M03+M04+M05+M06+M07+M08+M09+M10+M11+M12+M13+M14+M15+M16+M17+M18+M19+M20+M21+M22+M23+M24+M25+M26+M27 smoke test OK
fa_app.exe --frames 60 (Release)
             → m10: 60 update(s), frame 1/4, 5 event(s), last step@0, looping
             → m14: slot autosave, v1, scene demo, day 1, 1.00s, 7 entities, ...
             → m19: 3 tilled, 2 crop(s), 0 harvestable, 1 dead, ...
             → m20: 3 attack(s), 5 hit(s), 51 damage, 1 kill(s), ...
             → m21: attack, target valid (visible), 1 path node(s), ...
             → m22: year 1 spring day 1 09:40, sunny -> sunny, ambient 1.00, ...
             → m23: active, node greet, 2 choice(s), affinity 4, quest completed, ...
             → m24: gold 170 (+60 settled), seed 102, turnip 1, soup 2, ...
             → m25: quit, slot slot1, day 1, 1.50s, dirty no; 1 new / 1 load(s) /
                    1 save(s) / 2 autosave(s), 1 day end(s), ...; all ok
             → m27: disabled (release build)
             → m26: disabled (release build)
             → M02+M03+M04+M05+M06+M07+M08+M09+M10+M11+M12+M13+M14+M15+M16+M17+M18+M19+M20+M21+M22+M23+M24+M25 smoke test OK
```

### 未实现（0 个）

全部 28 个模块均已实现。剩余工作集中在 §5 的接线与 §6 的垂直切片/打磨项，
不再有“待编码模块”。

## 4. 已有的接口面（垂直切片/补丁可以直接用）

写后续补丁前先看这里，避免重复造轮子或造出冲突的抽象：

| 你需要的 | 现成的 | 位置 |
|---|---|---|
| 错误返回 | `core::Result<T>` / `core::Status` / `errorStatus(code, msg)` | `fa/core/Result.hpp`、`fa/core/Error.hpp` |
| 数学 | `Vec2`、`Rect`、`Rng` | `fa/core/` |
| 窗口 / 文件 / 时钟 | `IPlatform`、`IFileSystem`、`IGlContext` | `fa/platform/` |
| 音频设备 | `IAudioDevice`、`AudioCallback`（`platform.audio()`） | `fa/platform/AudioDevice.hpp` |
| 音频片段 | `AudioClip`（PCM16、`frameCount`/`durationSeconds`）、`makeTone`/`makeSilence` | `fa/audio/AudioClip.hpp` |
| 软件混音 | `AudioMixer`（`add`/`stop`/`setBusVolume`/`setListener`/`mix`/`stats`） | `fa/audio/AudioMixer.hpp` |
| 音频引擎 | `AudioEngine`（`initialize`/`shutdown`/`play`/`stop`/`setBusVolume`/`setListener`/`update`/`stats`） | `fa/audio/AudioEngine.hpp` |
| 音频参数 | `AudioBus`、`SoundDesc`、`SoundHandle`、`AudioStats`、`kMaxVoices` | `fa/audio/AudioTypes.hpp` |
| 无锁队列 | `SpscQueue<T, Capacity>` | `fa/audio/SpscQueue.hpp` |
| 调试控制台 | `DebugConsole`、`ConsoleCommand`、`splitCommandLine`、`trimCommandLine` | `fa/debug/DebugConsole.hpp` |
| CVar | `CVarRegistry`、`CVarValue`、`CVarInfo`、`CVarType` | `fa/debug/DebugCVar.hpp` |
| 调试日志 | `DebugLogSink`、`DebugLogLine`、`formatLogLine` | `fa/debug/DebugLog.hpp` |
| 调试统计 | `DebugStats`、`formatStats` | `fa/debug/DebugStats.hpp` |
| 实体检查 | `inspectWorld`、`EntityInspection`、`formatInspection` | `fa/debug/DebugInspector.hpp` |
| 调试覆盖层/门面 | `DebugOverlay`、`DebugTools`、`DebugOverlayState` | `fa/debug/DebugOverlay.hpp`、`fa/debug/DebugTools.hpp` |
| 物理调试绘制 | `drawPhysicsShapes` | `fa/debug/DebugDraw.hpp` |
| 存档值树 | `SaveArchive`（object/array/parse/child/set*/get*/dump） | `fa/save/SaveArchive.hpp` |
| 存档类型 | `SaveMetadata`、`SaveContext`、`SaveFile`、`kSaveVersion`、`kSaveMagic` | `fa/save/SaveTypes.hpp` |
| 存档服务 | `ISaveService`、`SaveService`（save/load/apply/listSlots/deleteSlot/registerMigration） | `fa/save/SaveService.hpp` |
| 组件序列化 | `IComponentSerializer`、`ComponentSerializerRegistry` | `fa/save/ComponentSerializer.hpp` |
| 存档错误码 | `save_error::*`（6100–6107） | `fa/save/SaveError.hpp` |
| 应用流程 | `GameFlow`、`IGameFlow`、`AppState`、`GameSession`、`FlowEvent`/`FlowStats`、`manualSlotName`、`saveToSlot` | `fa/flow/GameFlow.hpp`、`fa/flow/FlowTypes.hpp` |
| 睡眠规则 | `sleepHoursUntil`、`sleepRecovery`、`kWakeHour`/`kFullSleepHours`/`kMinSleepRecovery` | `fa/flow/Sleep.hpp` |
| 流程错误码 | `flow_error::*`（7400–7410） | `fa/flow/FlowTypes.hpp` |
| 动画片段 | `AnimationClip`、`AnimationFrame`、`Direction`、`directionFromVector` | `fa/anim/AnimationClip.hpp` |
| 动画播放 | `Animator`、`AnimationEvent`、`AnimationService`、`IAnimationService` | `fa/anim/Animator.hpp`、`fa/anim/AnimationService.hpp` |
| 动画状态机 | `AnimationStateMachine`、`AnimationParams`、`DirectionalClips`、`AnimationTransition` | `fa/anim/AnimationStateMachine.hpp` |
| 战斗判定 | `CombatService`、`ICombatService`、`applyDamage`/`spawnAttack`/`spawnProjectile`/`applyStatus`/`update` | `fa/combat/CombatService.hpp` |
| 战斗数据 | `Damage`、`AttackHitbox`、`Projectile`、`StatusEffect`、`LootSource`、四类事件、`CombatStats` | `fa/combat/CombatTypes.hpp` |
| 事件 / 线程池 | `EventBus`、`Subscription`、`JobSystem`、`parallelFor` | `fa/events/` |
| 主循环 | `FrameLoop`、`FrameContext` | `fa/time/` |
| 资源 | `IAssetManager`、`AssetHandle<T>`、`AssetId`、`AssetCatalog` | `fa/assets/` |
| 渲染 | `IRenderer`、`RenderQueue`、`Camera2D`、`Viewport`、`TextureHandle` | `fa/render/` |
| 输入 | `IInput`、`ActionId`、`ActionMap`、上下文栈 | `fa/input/` |
| 文字绘制 | `buildBuiltinFont`、`glyphFor`、`measureText`、`drawText`、`FontAtlas`、`TextStyle`、`TextMetrics`、`TextAlign` | `fa/ui/Font.hpp`、`fa/ui/Text.hpp` |
| 即时模式 UI | `UiContext`（`beginFrame`/`panel`/`border`/`progressBar`/`text`/`button`/`checkbox`/`slider`/`submit`）、`UiId`、`UiInputState`、`UiResult`、`UiTheme` | `fa/ui/Ui.hpp` |
| 物品栈 | `ItemStack`（`ContentId`/`count`/`durability`） | `fa/inventory/ItemStack.hpp` |
| 背包容器 | `Inventory`（`add`/`remove`/`removeAt`/`takeAt`/`swap`/`split`/`merge`/`transferTo`/`restore`）、`InventoryEvent`、`InventoryStats` | `fa/inventory/Inventory.hpp` |
| 快捷栏 / 装备 | `Hotbar`（`select`/`cycle`/`consumeSelected`）、`Equipment`（`Tool`/`Weapon`） | `fa/inventory/Hotbar.hpp`、`fa/inventory/Equipment.hpp` |
| 物品使用 | `IItemUseHandler`、`ItemUseContext`、`ItemUseService`、`ItemUseEvent` | `fa/inventory/ItemUseHandler.hpp`、`fa/inventory/ItemUseService.hpp` |
| 玩家容器 | `PlayerInventory`（`backpack`/`hotbar`/`equipment`/`moveToHotbar`/`moveToBackpack`） | `fa/inventory/PlayerInventory.hpp` |
| 背包错误码 | `inventory_error::*`（7019–7030） | `fa/inventory/InventoryError.hpp` |
| 实体与组件（**L2，玩法都用它**） | `World`、`EntityId`、`CommandBuffer`、`Transform`、`Sprite` | `fa/scene/` |
| 系统调度 | `ISystem`、`SystemPipeline`（`onFixedUpdate`/`onVariableUpdate`/`onRenderExtract`） | `fa/scene/System.hpp` |
| 场景 | `IScene`、`BasicScene`、`SceneStack` | `fa/scene/` |
| 世界 → 绘制列表 | `SpriteExtractSystem`、`extractSprites`、`SpriteExtractOptions` | `fa/scene/SpriteExtractor.hpp` |
| 瓦片地图（**地形/耕地/区域都用它**） | `Tilemap`、`TileId`、`TileCoord`、`TileBounds`、`TileDef`、`TileLayer`、`MapArea`、`MapPortal`、`MapAmbient` | `fa/tilemap/Tilemap.hpp`、`TileId.hpp`、`TileDef.hpp` |
| 地图查询 | `worldToGrid`/`gridToWorld`、`tileAt`/`setTile`/`fillLayer`/`setBorder`、`isWalkable`/`isWater`/`isTillable`/`isSolidCell`、`queryArea`/`portalAt` | `fa/tilemap/Tilemap.hpp` |
| 地图接入物理 | `TilemapSolids`（实现 `physics::ISolidTiles`），直接传给 `PhysicsSystem` | `fa/tilemap/Tilemap.hpp` |
| 地图 → 绘制列表 | `TilemapExtractSystem`、`extractTiles`、`TileExtractOptions`、`TileExtractStats` | `fa/tilemap/TilemapExtract.hpp` |
| 地图文件 | `loadTilemapFromJson` | `fa/tilemap/TilemapLoader.hpp` |
| 自动瓦片 | `AutotileRule`、`neighborMask`、`bakeAutotiles` | `fa/tilemap/Autotile.hpp` |
| 交互目标（**玩法都用它**） | `InteractionService`（`pickTarget`/`pickTargetInFront`/`canInteract`/`interact`）、`GridTarget`、`Facing`、`ToolKind`、`InteractionOptions` | `fa/interaction/InteractionService.hpp`、`InteractionTypes.hpp` |
| 可交互实体 | `Interactable` 纯数据组件（局部 `area`/`priority`/`payload`） | `fa/interaction/Interactable.hpp` |
| 工具行为扩展 | `IToolHandler`、`ToolUseContext`；M19/M20 注册具体工具 | `fa/interaction/ToolHandler.hpp` |
| 格子放置 | `PlacementGrid`（占用/多格/按 owner 移除）、`PlacementRequest`、`PlacementPreview` | `fa/interaction/PlacementGrid.hpp`、`InteractionTypes.hpp` |
| 交互事件 | `InteractionEvent`、`InteractionEventKind` | `fa/interaction/InteractionTypes.hpp` |
| 玩家输入 → 意图 | `PlayerSystem`、`PlayerIntent`、动作名 `Run`/`Dodge`/`UseTool`/`Attack`/`Interact`/`Inventory` | `fa/player/PlayerSystem.hpp`、`PlayerTypes.hpp` |
| 玩家状态与数据 | `PlayerTag`、`FacingDir`、`PlayerStateComp`、`Stamina`、`Health`、`Wallet`、`DodgeState` | `fa/player/PlayerTypes.hpp` |
| 玩家动作与闪避 | `PlayerController`、`PlayerActionRequest`、`isDodging`/`isInvulnerable`、`PlayerAnimationParams` | `fa/player/PlayerController.hpp` |
| 内容 ID 与定义 | `ContentId`、`ItemDef`、`CropDef`、`RecipeDef`、`EnemyDef`、`LootTableDef`、`LootEntry`、`Season` | `fa/content/ContentId.hpp`、`ContentTypes.hpp` |
| 内容数据库 | `ContentDatabase`（`loadDocument`/`validate`/按 ID/标签/类别查询/`stats`/`errors`） | `fa/content/ContentDatabase.hpp` |
| 内容错误 | `ContentError`、`content_error::*` | `fa/content/ContentError.hpp` |
| 碰撞体 | `Collider`、`CollisionLayer`、`CollisionMask`、`maskOf`、`colliderRect`、`layersInteract`、`kPlayerFeetCollider` | `fa/physics/Collider.hpp`、`CollisionLayer.hpp` |
| 移动与查询（**玩法都用它**） | `PhysicsWorld`（`move`/`step`/`overlap`/`overlapCircle`/`raycast`）、`Velocity`、`MoveResult`、`RayHit` | `fa/physics/PhysicsWorld.hpp` |
| 物理接入帧循环 | `PhysicsSystem`（固定步驱动）、`triggerEvents()` | `fa/physics/PhysicsSystem.hpp` |
| 静态地形 | `ISolidTiles`（M12 的 `TilemapSolids` 已实现）、`TileGrid`（现成的位图网格）、`overlapsSolidTile` | `fa/physics/TileCollision.hpp` |
| 触发器事件 | `TriggerEvent`、`TriggerPhase` | `fa/physics/PhysicsWorld.hpp` |
| 碰撞调试绘制 | `PhysicsWorld::debugDraw` → `DebugShape` 列表 | `fa/physics/PhysicsWorld.hpp` |

错误码分段（`Error.hpp`，加新码前先看自己该在哪个段）：
1000–1999 Core、2000–2999 Platform、3000–3999 Assets、**4000–4999 Render/Input/Audio/UI**、
**5000–5999 Scene（5000–5007）/ Physics（5008–5012）/ Tilemap（5013–5022）**、**6000–6999 Save/Content（M15 6000–6008）**、
**7000–7999 Gameplay（M17 7000–7012 / M16 7013–7018 / M18–M24 7019–7313 / M25 7400–7410）**。

> 玩法模块（M15–M25）**不要**自己再造一套实体/组件管理：加组件就 `world.add<MyComponent>(id, ...)`，
> 每帧逻辑就写一个 `ISystem` 挂到 `systems().add(...)`。反向依赖（`fa_scene` include `fa/game/...`）是硬禁止。

> 也**不要**自己写碰撞或自己改 `Transform::position`：位移交给 `PhysicsWorld::move`/`step`，
> 判定用 `overlap`/`raycast`。位置有两个写入方时，结果会取决于系统注册顺序。

## 5. 跨模块遗留风险

| 风险 | 影响 | 归属 |
|---|---|---|
| `kVirtualHeight = 360` 不是 16 的整数倍 | 纵向 22.5 格；演示地图取 23 行，相机夹取会裁掉底部半格；更大的地图按 23 行布局或改 640×352 | M10/M12（已按 23 行落地） |
| 旋转精灵破坏像素风 | `DrawItem::rotation` 只靠注释限制 90° 倍数 | M06（接受） |
| 文字只支持 ASCII | 多字节 UTF-8 会拆成多个 `?`；CJK 需要解码 + 动态图集 | M09 |
| 内置 3×5 字体可读性有限 | 只适合占位与调试；真实字体后置 | M09 |
| UI 仅鼠标 | 键盘/手柄焦点链、Tab 顺序、文本输入未实现 | M09 / M25 |
| UI 按 `sortY` 排序 | 重叠窗口的绘制顺序由屏幕 y 决定，不是提交顺序 | M09 |
| UI 无布局组/裁剪/滚动 | 复杂面板要手算矩形，超出面板仍会绘制；字号由 `ui.scale` 放大，极端比例可能溢出 | M09 / M18 / M26 |
| 窗口只限制最小尺寸 | 允许放大，最小 1280×720；非 16:9 时视口/相机的适配仍依赖现有逻辑 | M02 / M06 |
| 配置字段缺失即回落 | `config/game.json` 逐字段保留默认值；非法值会被夹取，不会阻止启动 | app |
| UI 不自动切输入上下文 | 打开 UI 时玩法输入仍生效，需调用方屏蔽 | M09 / M25 |
| 耐久只参与分栈不参与移除 | `remove(id,count)` 会合并不同耐久的计数，返回耐久取自首槽 | M18 / M20 |
| 使用不自动消耗 | handler 忘记 `removeAt` 会无限使用 | M18 / M19 / M20 |
| 无重量/容量/分类限制 | 任何物品可进任何容器；Tool 槽可以放食物 | M18 / M19 |
| `ContentDatabase` 生命周期 | 容器存借用指针，内容库先销毁会让 `maxStackFor`/use 悬空 | M18 |
| 背包事件只记录不发布 | M03 接线由组装根决定；UI 需要自己清事件 | M18 / M03 |
| 无音频解码器 | 现在放一个 WAV/OGG 进去也读不出来；只有代码生成的 PCM | M05 注册 `IAudioDecoder` 后接 `AudioClip` |
| 音频整段常驻 / 无流式 | 长音乐全部占内存，切换曲目有加载尖峰 | M08（后续 `MusicStream`） |
| 固定 48 kHz / 1–2 声道 | 其他设备格式依赖 SDL 流转换；多声道不做 | M08（需要时扩展 `IAudioDevice`） |
| `play()` 句柄早于执行 | 返回非零不代表 voice 一定存活；淘汰/队列满只计数 | M08（需要反馈时加事件） |
| 音频统计是回调快照 | 主线程读到的 active/played 可能落后一帧 | M08 / M27（面板刷新时读即可） |
| 距离衰减是简单反比 | 不是物理正确的 3D 衰减；只适合 2D 近似 | M08 / M22 |
| 无总线 DSP / 低通 / 混响 | 环境音、室内外过滤做不了 | M08 扩展点 / M22 |
| CI 可能没有音频设备 | `SDL_OpenAudioDeviceStream` 失败时静默降级 | M08 已非致命（与 GL 相同） |
| 调试代码泄漏到发布构建 | Release 可执行文件包含控制台/CVar/覆盖层 | M27（条件 ProjectReference + `FA_ENABLE_DEBUG_TOOLS` 守卫） |
| 控制台只支持 ASCII | CJK 输入会拆成多个 `?` | M27 / M09 |
| 调试命令绕过玩法规则 | `teleport`/`give` 不受约束，只用于开发期 | M27 |
| 日志自旋锁高频烧 CPU | 大量日志时会占用一个核心 | M27（开发期可接受） |
| 覆盖层固定像素布局 | 非 640×360 时面板可能重叠 | M27 / M09 |
| 实体检查器只读且上限 64 | 不能搜索/编辑/销毁实体 | M27 |
| 无 ImGui / 时间控制 / 纹理查看 | 复杂面板与暂停单步仍缺 | M27 后续 |
| 无动画混合 | 上下半身/武器叠加/平滑过渡做不了 | M10 后续 |
| 动画仍是四向 | 玩家朝向已八向，但 M10 `Direction` 只有四向；斜向需复用相邻主方向或由组装根映射 | M10 / M16 |
| 方向切换重启 clip | 走/跑切换可能有相位跳变 | M10（v1 取舍） |
| 事件只有字符串 | 攻击窗口/特效需要结构化参数 | M10 / M20 |
| 无动画资源格式 | clip 由代码构建，真实数据驱动缺 loader | M10 / M05 / M26 |
| 攻击窗口仍由 app 手动触发 | 没有把 M10 动画事件自动接到 M20 | M10 / M20 |
| 矩形 hitbox 不随朝向旋转 | 扇形/朝向判定未实现 | M20 |
| 击退直接 move | 没有受击硬直/速度衰减，墙边可能夹住 | M20 / M13 |
| 死亡只走自动复活 | 荆棘扣血后床边复活并扣 10% 金币；没有主菜单/读档选项 UI，死亡掉落仍只在敌人死亡时生成 | M20 / M15 / M18 / M25 |
| 状态同类刷新不叠加 | DoT 为整数 tick，低 dps 有延迟 | M20 |
| 投射物每步 raycast+move | 大量投射物需要对象池/批量查询 | M20 |
| 没有通用组件 serializer | 背包/快捷栏/装备/农田/经济由 app 手写字段；AI、叙事、世界对象仍需逐模块接线 | M14 / M19 / M20 / M23 / M25 |
| `.bak` 只有一份 | 连续两次写坏仍可能丢失 | M14 |
| 没有 CRC/压缩/加密 | 磁盘位翻转检测不到；大存档体积大 | M14 后续 |
| 存档 JSON 深拷贝 | 大存档有分配开销 | M14（不在每帧热路径） |
| 读档应用时机未定 | M25 已定义 `LoadHandler` 时机；app 先按存档切换地图，再恢复日历/位置/背包/农田，敌人等资源引用仍需更晚的应用阶段 | M14 / M25 |
| 自动存档调度未接 | M25 已按天/退出调度；每 12 游戏小时与安全状态判定仍未实现 | M25 / M22 |
| 无渲染目标 / 后处理 | 做不了光照、屏幕扭曲 | M22 需要时再加 `RenderPass` |
| 单一混合模式 | 预乘 alpha、加色做不了 | M06（分批断点已留好） |
| 图片仍是占位美术 | 已支持 `assets/*.png`，当前 11 张图是简单像素占位 | 真实美术后置，见 [art-policy](architecture/art-policy.md) |
| 只接了 PNG 解码 | app 注册的是 stb_image；JPEG/WebP 等其他格式仍需扩展解码器 | M05（注册 `IImageDecoder`） |
| 隐藏窗口依赖驱动 | 无 GL 驱动的 CI 镜像拿不到上下文 | M06 已降级为非致命 |
| `/EHsc` 依赖第三方 include 图 | 新增 JSON/`<chrono>` 依赖会连锁要求 `/EHsc` | M00 规约 |
| ECS 无实体原型 / prefab | 建复杂实体要在代码里逐行 `add`；内容驱动的实体要等 prefab 系统 | M11（M15 已提供定义，仍缺 prefab） |
| `Sprite` 只存裸 `TextureHandle` | 纹理被 asset manager 卸载后句柄悬空 | M11 + M05 热重载 |
| `SparseSet::remove` 打乱遍历顺序 | 依赖顺序的逻辑会偶发错乱；要顺序得自己排序 | 各玩法模块 |
| 实体对实体只有步首快照 | 一步之内两个实体挤进同一格会重叠一帧 | M13（已知取舍） |
| 卡在墙里的实体能穿那面墙 | 生成点若在墙内会一直穿模，直到离开 | M17/M19（生成/放置前校验） |
| 触发器每步全量重算 | 触发器上百时成为热点 | M13 / M19 |
| `Tilemap::defFor` 线性查找 | 瓦片定义上百时每次查询都扫一遍 | M12（内容表变大后加索引） |
| `Tilemap::stats()` 全量遍历 | 每帧调用会拖慢大图 | M12 / M27（只在面板刷新时调） |
| 自动瓦片是烘焙的 | 编辑瓦片后必须重新烘焙，运行时不会自动重算 | M12 / M26 |
| 地图全量载入内存 | 128×128×3 层可接受；更大需要分块 | M12 |
| 换地图会重建场景 | app 已用 `SceneStack::replace` 在两张 40×23 地图间切换；农田/背包/叙事/玩家生命耐力放在 `GameState`，敌人/AI/掉落等场景内实体默认重置 | M11 / M25 |
| 地图纹理按名称映射 | app 用 `textureForName` 把 JSON 的 `texture` 绑到 catalog；未识别的名称会回落 soil，不会报错 | M12 / M26 |
| M17 的 `payload` 是临时内容 ID | M15 已实现，接线时统一替换，避免两套 ID | M15 / M17 |
| 放置占用与实体碰撞是两套数据 | 放置成功但没创建碰撞体会导致“看得见、能穿过” | M19（放置后创建碰撞体） |
| 放置不检查已有实体 | 物品可能和 NPC/掉落物同格 | M17 / M19（放置前校验） |
| 家具只能轴对齐 | 旋转靠交换宽高，非正方形家具语义不完整 | M17 / M26 |
| 移除按 owner 清空所有格 | 多格对象移除不会通知其他系统 | M17 / M19 |
| 视线是单中心线 | 大体型 actor 可能穿过窄缝 | M17（需要时改体积扫描） |
| `canInteract` 每次重算距离/视线 | 逐帧扫大量目标会变热 | M17（缓存目标） |
| `Interactable` 不随缩放/旋转 | 变换后局部矩形与视觉不一致 | M17 / M10 |
| 冒烟标记是 8×8 | 不是 16×24 角色；真实玩家需 `kPlayerFeetCollider` 10×6 | M16 / 美术规范 |
| 动作锁固定 0.2 s | 轻/重攻击与不同工具没有独立手感 | M16 / M20 |
| 闪避期间禁止其他动作 | 是否允许闪避取消攻击未定 | M16 / M20 |
| 多玩家共享同一 `PlayerIntent` | v1 只支持一个玩家；多玩家需要每实体意图 | M16 |
| `Hurt`/`Menu` 状态未使用 | 受击与菜单需要 M20/M09/M25 接入 | M16 / M20 / M25 |
| 玩家动作只记录不发布 | M03 事件化由组装根决定 | M16 |
| 内容加载不是事务 | 失败时会保留已解析的数据，便于修好后重试 | M15（多文件发布要小心） |
| 内容指针会失效 | 继续加载/热重载会让 `vector` 重分配 | M15（实体只存 `ContentId`） |
| 内容不校验资源存在性 | `AssetId` 只校验语法，catalog 没声明的纹理也能通过 | M05/M15 |
| 内容前缀规则很严 | `item.`/`crop.`/`recipe.`/`enemy.`/`loot.` 写错就报错 | M15（内容约定） |
| 内容表没有瓦片/商店/对话/任务 | M12 的 `TileDef`、M23 的叙事定义与 M24 的 `ShopDatabase` 仍是独立表，尚未迁入 M15 | M15/M12/M23/M24 |
| 内容版本不是内容哈希 | `contentVersion` 只是加载次数，跨存档兼容还要 M14 决定 | M15/M14 |
| 宽相 cell 尺寸固定 64 px | 大量超大碰撞体会推高 `maxCellOccupancy`，查询多捞 | M13 调优 |
| 无视锥/扇形查询 | AI 感知只能用 `overlap` + 自己过滤 | M21 |
| `Velocity` 是否进存档未定 | 速度是意图还是状态，会影响 M14 的格式 | M14 |
| 系统注册顺序是隐式耦合 | 顺序错了得到难查的 bug | 各玩法模块 + M27 |
| 场景与世界如何存档未定 | 当前只保存“哪张地图 + 玩家位置 + 农田/背包/经济”，敌人/AI/世界对象不保存 | M14 / M25 |
| 无场景过渡效果 | 切场景是硬切 | M25 |
| 无实体原型 → 世界规模 | 单场景 1e3 量级；10 万实体缓存局部性差 | M15 已实现；prefab 落地后再评估 |
| 睡眠没有强制起床 | 任意时间可睡并按 8 h 上限衰减恢复；没有晕倒、强制打断或睡眠惩罚 | M25 / M16 |
| 天气只有日粒度 | 没有连续天气/预报准确率/区域差异 | M22 后续 |
| 环境光是近似暗化 | 不是乘法光照；没有法线、阴影、彩色光通道 | M22 / M06 后续 |
| 点光无遮挡/加法混合 | 只是半透明 quad，墙后也会亮 | M22 / M06 后续 |
| 天气效果只提供查询 | 粒子/音效/室内外过滤要 app、M08、M12 接线 | M22 / M08 / M12 |
| 天气 RNG 需手动存档 | 只保存 `today/tomorrow` 会让读档后的下一次掷点漂移 | M14 / M22（已暴露 `weatherState`） |
| 农田快照是整体替换 | `restore()` 不校验格子/可耕地，与当前内容定义不一致的作物会原样恢复 | M14 / M19（调用方过滤） |
| 农田按格线性查找 | 大田的查找与日结算是 O(n)，v1 规模可接受 | M19（需要时换哈希/索引） |
| 作物产量/品质未实现 | 单株单产，没有品质概率、多次收获次数与产量表 | M19 后续 |
| 箱子/熔炉仍是占用层 | 加工由 M24 服务处理，但没有设备实体/燃料/队列；箱子存储与洒水器行为未实现 | M19 / M24 |
| 作物表现仍基础 | 已使用 `CropDef::seedSprite`/`stageSprites` 的 4 阶段贴图；死亡/再生长的视觉反馈仍只有颜色变化 | M19 / M05 |
| 杂物未进 app 演示 | `DebrisLayer` 与斧/镐路径只有单元测试覆盖 | M19 |
| 寻路没有任务池/每帧上限 | 大量 agent 同时重算会推高固定步耗时 | M21 后续 |
| 感知只有视野 | 没有听觉、区域仇恨、群体编队/包围 | M21 后续 |
| 没有 NPC 日程 | M22 的 `HourChanged` 尚未接到 AI | M23 / M25 |
| 没有 AI 存档 | 读档后 agent 回到初始状态 | M14 / M25 |
| Flee 只回出生点 | 没有逃跑方向评分或安全点选择 | M21 后续 |
| 没有卡角落脱困 | 被动态障碍夹住时可能停在原地 | M21 后续 |
| 编辑器只做瓦片 | 对象/NPC/敌人/传送点/属性面板/内容检查器未实现 | M26 后续 |
| 编辑器沿用运行时相机 | 没有自由平移/缩放/网格吸附 | M26 后续 |
| 编辑后不重烘焙自动瓦片 | 新画的格子按原 tile id 提取 | M12 / M26 |
| L 键只校验加载 | 不替换实时地图，避免丢失纹理绑定 | M26 后续 |
| 编辑器面板是固定坐标 | 非 640×360 时可能与其它覆盖层重叠 | M09 / M26 |
| Release 不链接编辑器 | 需要 `FA_ENABLE_EDITOR` 与非 Release 引用 | 构建约定 |
| 叙事定义没有进 M15 | M23 自带 `NarrativeDatabase`，物品引用只校验前缀 | M15 / M23 |
| 叙事没有存档 | 好感度/任务/flags 还不能写入 `SaveArchive` | M14 / M23 / M25 |
| 对话不暂停玩法 | M25 有 Dialogue 状态并暂停时钟，但 app 尚未在对话开始时调用 `enterDialogue()` | M07 / M23 / M25 |
| 没有 NPC 日程 | M22 的 `HourChanged` 与 M21 尚未接到 M23 | M21 / M23 / M25 |
| 任务奖励不是事务 | 背包满时物品可能丢失，任务仍完成 | M18 / M23 / M24 |
| `Deliver` 不消耗物品 | 收集/送达只按事件推进，不检查背包 | M18 / M23 |
| 金币奖励没有走 M24 | M23 只记录金币事件；M24 钱包尚未被任务调用 | M23 / M24 |
| 没有任务日志/本地化 | 文本单字符串，无滚动日志、无翻译 | M09 / M23 |
| 商店/配方表没有进 M15 | M24 自带 `ShopDatabase`；配方仍来自 M15，商店未跨表 | M15 / M24 |
| 金币没有统一钱包 | M24 持有 `int m_gold`，尚未写回 `player::Wallet` | M18 / M24 |
| 商店面板是基础版 | 只提供买种子/卖萝卜；没有分类列表、季节库存与价格波动 | M09 / M22 / M24 |
| 加工站是字符串 | 没有设备实体、燃料、队列上限或多输出 | M11 / M24 |
| 经济存档只覆盖核心状态 | 金币/出货清单/加工任务/每日余量已保存；统计与季节库存不保存 | M14 / M24 / M25 |
| 睡眠恢复是正式规则 | 任意时间可睡；满 8 h 回满，不足按 `(h/8)²` 衰减至最低 1%；没有晕倒/强制打断 | M19 / M22 / M24 / M25 |

## 6. 下一步建议

> **全部 28 个模块已实现。** 下一步不是继续选模块，而是把现有能力接成可玩的垂直切片并打磨。

| 方向 | 理由 | 主要风险 |
|---|---|---|
| 叙事接入 play | M23 服务/测试/对话框已实现，但 NPC 交互未接线，任务在 play 不可达；先接它收益最大 | 输入上下文与任务奖励事务 |
| 流程打磨 | 死亡后的主菜单/读档选项、菜单键盘焦点、场景切换过渡 | 输入上下文与暂停状态 |
| 战斗打磨 | 当前是最小近战盒；还需攻击动画/朝向反馈、敌人反击、掉落持久化 | 攻击窗口与动画同步；掉落物存档 |
| 其余存档段 | 叙事/AI/世界对象写入 `SaveFile` 并读回 | 各模块 serializer 与读档应用时机 |
| 商店与任务面板 | 出货箱/商店/背包面板已接；还需季节库存、任务日志与可滚动分类背包 | M09 面板与 M17 目标选择 |
| 内容与手感 | 真实美术/音频、数值平衡、对话/商店/任务面板、八向动画 | 内容量与时序；像素风一致性 |
| 工程收尾 | 全新克隆 CI、性能基线、存档迁移链 | 第三方依赖与 CI runner 差异 |

**种植、战斗、睡眠/存档、菜单/背包、出货箱/商店、林地传送与死亡复活已可用 `--play` 手动验证；独立地图与 `SceneStack::replace` 已接通。M23 叙事仍只在脚本演示/测试中可达，死亡菜单/读档选项及叙事/AI/世界对象存档仍需组装根接线。**
后续每项改动仍按既有节奏推进：
**设计/确认 → 实现 → 测试 → app 集成 → 三配置验证 → 全新克隆验证 → 回写文档 → 更新本文件 → 提交**。

## 7. 维护方式

每次交付完成后更新本文档的以下几处，这是「方便后续其他对话继续任务」的兑现方式：

1. §3 把模块从「未实现」移到「已实现」，填用例数与关键交付；
2. §3 的验收快照换成最新数字；
3. §5 增删跨模块风险；
4. §6 重排下一步建议；
5. 头部「最后更新」写上新的提交号（本次改动尚未提交，交付时补）。

历史交付顺序与当时的细节，看各模块的 `docs/build/mXX-*.md`；决策背后的一次性权衡，看 [ADR](adr/README.md)。
