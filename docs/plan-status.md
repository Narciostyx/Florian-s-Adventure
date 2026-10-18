# 计划完成情况

**这份文档是「继续任务」的唯一入口。** 新开一个对话继续开发时，先读这里，再读对应的模块文档与构建记录，不需要重新翻历史。

- 最后更新：M09 UI & Text 交付完成（实现 + 文档回写）
- 当前总体：**28 个模块中 15 个已实现**（M00–M07 + M09 + M11 + M12 + M13 + M15 + M16 + M17），其余处于设计完成、尚未编码
- 当前可运行内容：窗口 + 主循环 + 事件/作业 + 输入 + 渲染 + 资源缓存 + **ECS 世界与场景栈** + **AABB 碰撞与查询** + **瓦片地图与图层提取** + **交互目标选择与格子放置** + **玩家移动/朝向/耐力/闪避** + **带引用校验的内容数据库** + **内置位图字体与即时模式 HUD/控件**；画面是**全部色块占位**，40×23 地图（920 格、143 实心、16 可耕地）由 M12 提取，标记由 M16 驱动、M13 求解并停在 x=344，M17 在标记旁选目标、调用一次工具并占一个放置格，M15 加载物品/作物/配方/敌人/掉落表，M09 每帧绘制 HUD、耐力条、按钮、复选框与滑条，但还没有完整玩法

## 1. 长期有效的规约

这几条来自用户明确要求，**任何后续改动都必须遵守**：

| 规约 | 说明 |
|---|---|
| 源码只写 Doxygen 注释 | `///`、`//!`、`\brief`、`\param`；唯一例外是 `}  // namespace fa::core` 这类工具锚点。踩坑、背景、权衡**一律写进 `docs/build/mXX-*.md`**，不写进源码 |
| 美术一律色块占位 | 不引入真实图片；纹理由 catalog 的 `color`/`altColor`/`tile` 程序化生成。见 [美术资源策略](architecture/art-policy.md) |
| 文档必须同步 | 每次交付同时更新：模块文档（`docs/modules/`）、构建记录（`docs/build/`）、本文件、[索引](index.md) |
| 三个配置都要过 | Debug / Development / Release × x64，构建零 warning（`/W4 /WX`），`fa_tests.exe` 全绿，`fa_app.exe --frames 60` exit 0 |
| 不引入新依赖除非必要 | M06 自研 GL 函数表而不引 glad；M05 用反向接口而不让 `fa_assets` 依赖 `fa_render` |

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

### 已实现（15 个）

| 模块 | 名称 | 用例（新增） | 关键交付 | 文档 |
|---|---|---|---|---|
| M00 | Build & Dependency Infrastructure | — | `.sln` + 8 个 `.vcxproj`、`vcpkg.json`、`Directory.Build.props`、`scripts/build.ps1`、CI | [设计](modules/00-build-infrastructure.md) · [实测](build/m00-skeleton.md) · [踩坑](build/m00-fixups.md) |
| M01 | Core Foundation | 22 | `Result`/`Status`/`Error`、`Vector2`、`Rect`、`Rng`、断言与日志 | [设计](modules/01-core-foundation.md) |
| M02 | Platform Layer | 8 | 窗口、事件泵、`IFileSystem`（含原子写）、单调时钟、消息框、`IGlContext` | [设计](modules/02-platform-layer.md) · [实测](build/m02-platform.md) |
| M03 | Events & Jobs | 26 | `EventBus` + RAII `Subscription`、`JobSystem` 线程池与 `parallelFor` | [设计](modules/03-events-and-jobs.md) · [实测](build/m03-events-jobs.md) |
| M04 | Time & Frame Loop | 20 | 固定步时钟、`FrameContext`、时间域暂停/缩放、`FrameLoop` | [设计](modules/04-time-and-frame-loop.md) · [实测](build/m04-frame-loop.md) |
| M05 | Asset Pipeline | 61 | `AssetId`/`AssetHandle`、`AssetCatalog`、异步加载 + 帧线程上传、占位降级、`ITextureUploader` 反向桥 | [设计](modules/05-asset-pipeline.md) · [实测](build/m05-assets.md) |
| M06 | Rendering | 42 | `IRenderer` + `RenderQueue` + `Camera2D` + `Viewport`、自研 42 项 GL 函数表、精灵批处理、调试绘制 | [设计](modules/06-rendering.md) · [实测](build/m06-render.md) |
| M07 | Input | 36 | `ActionId` + `Binding` + 每上下文 `ActionMap`、上下文栈、`IInput` 窄接口、JSON 键位持久化 | [设计](modules/07-input.md) · [实测](build/m07-input.md) |
| M09 | UI & Text | 16 | 内置 3×5 ASCII 位图字体、字形回退、测量/换行/对齐、投影/描边、立即模式面板/按钮/复选框/滑条/进度条、`RenderLayer::UI` 与主题 | [设计](modules/09-ui-and-text.md) · [实测](build/m09-ui-and-text.md) |
| M11 | Scene / World & ECS | 78 | `World` + `EntityId` 代际句柄 + `SparseSet` + `CommandBuffer`、`SystemPipeline` 三钩子调度、`SceneStack` 栈式场景、`SpriteExtractor` 纯数据提取 | [设计](modules/11-scene-world-ecs.md) · [实测](build/m11-scene-ecs.md) |
| M12 | Tilemap & World | 17 | `Tilemap` + `TileId`/`TileCoord`/`TileBounds` + 多图层 + 瓦片属性、`worldToGrid`/`gridToWorld`/`queryArea`/`portalAt`、JSON 地图加载、4 邻接自动瓦片、`extractTiles` 视锥裁剪、`TilemapSolids` 物理适配器 | [设计](modules/12-tilemap-and-world.md) · [实测](build/m12-tilemap.md) |
| M13 | Physics & Collision | 40 | `Collider` + `CollisionLayer` 双向掩码、`SpatialHash` 宽相、`TileGrid` 静态碰撞、轴分离 + ≤4px 子步求解、`overlap`/`overlapCircle`/`raycast`、触发器进出事件 | [设计](modules/13-physics-collision.md) · [实测](build/m13-physics-collision.md) |
| M15 | Content Database | 11 | `ContentId` + `ItemDef`/`CropDef`/`RecipeDef`/`EnemyDef`/`LootTableDef`、季节掩码、JSON 多文档合并、前缀/重复/必填/数值校验、四类引用完整性、全量错误收集、按 ID/标签/类别查询 | [设计](modules/15-content-database.md) · [实测](build/m15-content-database.md) |
| M16 | Player Controller | 14 | `PlayerController` 走/跑/加速度/斜向归一化、朝向主导轴、状态机与动作锁、耐力/闪避/无敌窗口、`PlayerSystem` 输入适配、`PlayerActionRequest` 记录、动画参数 | [设计](modules/16-player-controller.md) · [实测](build/m16-player-controller.md) |
| M17 | Interaction & Grid Placement | 15 | `InteractionService` 目标选择（实体优先/朝向/距离/视线）、`Interactable` 组件、`IToolHandler` 分发、`PlacementGrid` 多格占用、`PlacementPreview`、交互/放置事件记录 | [设计](modules/17-interaction-grid-placement.md) · [实测](build/m17-interaction-grid.md) |

实际落地顺序是 M01 → M02 → M04 → M03 → M07 → M06 → M05 → M11 → M13 → M12 → M17 → M16 → M15 → M09，所以上表顺序与时间顺序不同。
M12 排在 M13 之后，是因为它要适配 M13 的 `ISolidTiles`；M17 排在 M12 之后，因为它要用地图做目标与放置校验；
M16 把输入、物理和 M17 的交互请求串起来；M15 是 P0，后续 M18/M19/M24 都靠它；
M09 最后落地，补上了此前「任何文字都画不出来」的空白，并为 M18/M23/M25 的界面做准备。

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

**验收快照**（测试三配置一致；冒烟三配置都是 exit 0。固定步数由墙钟累积，并行负载下可能明显多于 60，不要逐字比对帧数或 y 坐标；x 坐标才是稳定断言）：

```text
fa_tests.exe → All tests passed (18532 assertions in 406 test cases)
             "[ui]"       → 111 assertions in 16 test cases
             "~[ui]"      → 18421 assertions in 390 test cases
             "[content]"  → 178 assertions in 11 test cases
             "~[content]" → 18243 assertions in 379 test cases
             "[player]"   → 107 assertions in 14 test cases
fa_app.exe --frames 60 → M02+M03+M04+M05+M06+M07+M09+M11+M12+M13+M15+M16+M17 smoke test OK
```

### 未实现（13 个）

| 模块 | 名称 | 优先级 | 依赖是否已就绪 | 备注 |
|---|---|---|---|---|
| M08 | Audio | P1 | 需要 M05 ✅ | 需要 SDL_mixer 或自研混音；首个真正的新增第三方依赖点 |
| M10 | Animation | P1 | 需要 M05 ✅、M06 ✅ | 剪辑/混合/状态机边界；`revision` 已为它准备 |
| M14 | Serialization & Save | P1 | 需要 M11 ✅ | 多槽存档、游戏时间每 12 小时自动存档 |
| M18 | Inventory & Items | P1 | 需要 M15 ✅、M09 ✅ | |
| M19 | Farming & World Objects | P1 | 需要 M12 ✅、M17 ✅、M22 | 作物、耕地、温室 |
| M20 | Combat | P1 | 需要 M13 ✅、M16 ✅、M18 | 含耐力与闪避（ADR-0002） |
| M21 | AI | P1 | 需要 M11 ✅、M13 ✅ | |
| M22 | Calendar / Weather / Lighting | P1 | 需要 M04 ✅、M06 ✅ | 季节驱动 M19/M21 行为差异 |
| M23 | NPC / Dialogue / Quests | P2 | 需要 M09 ✅、M15 ✅ | |
| M24 | Economy / Crafting / Shops | P2 | 需要 M15 ✅、M18 | 星露谷式加工链 |
| M25 | Game Flow | P1 | 需要 M11 ✅、M09 ✅、M14 | 场景栈、菜单流 |
| M26 | Editor Tools | P2 | 需要 M05 ✅、M06 ✅ | |
| M27 | Debug Tools | P1 | 需要 M06 ✅ | `AssetStats` 已为它的资源面板准备 |

## 4. 已有的接口面（下一个模块可以直接用）

写新模块前先看这里，避免重复造轮子或造出冲突的抽象：

| 你需要的 | 现成的 | 位置 |
|---|---|---|
| 错误返回 | `core::Result<T>` / `core::Status` / `errorStatus(code, msg)` | `fa/core/Result.hpp`、`fa/core/Error.hpp` |
| 数学 | `Vec2`、`Rect`、`Rng` | `fa/core/` |
| 窗口 / 文件 / 时钟 | `IPlatform`、`IFileSystem`、`IGlContext` | `fa/platform/` |
| 事件 / 线程池 | `EventBus`、`Subscription`、`JobSystem`、`parallelFor` | `fa/events/` |
| 主循环 | `FrameLoop`、`FrameContext` | `fa/time/` |
| 资源 | `IAssetManager`、`AssetHandle<T>`、`AssetId`、`AssetCatalog` | `fa/assets/` |
| 渲染 | `IRenderer`、`RenderQueue`、`Camera2D`、`Viewport`、`TextureHandle` | `fa/render/` |
| 输入 | `IInput`、`ActionId`、`ActionMap`、上下文栈 | `fa/input/` |
| 文字绘制 | `buildBuiltinFont`、`glyphFor`、`measureText`、`drawText`、`FontAtlas`、`TextStyle`、`TextMetrics`、`TextAlign` | `fa/ui/Font.hpp`、`fa/ui/Text.hpp` |
| 即时模式 UI | `UiContext`（`beginFrame`/`panel`/`border`/`progressBar`/`text`/`button`/`checkbox`/`slider`/`submit`）、`UiId`、`UiInputState`、`UiResult`、`UiTheme` | `fa/ui/Ui.hpp` |
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
**7000–7999 Gameplay（M17 7000–7012 / M16 7013–7018）**。

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
| UI 无布局组/裁剪/滚动 | 复杂面板要手算矩形，超出面板仍会绘制 | M09 / M18 / M26 |
| UI 不自动切输入上下文 | 打开 UI 时玩法输入仍生效，需调用方屏蔽 | M09 / M25 |
| 无渲染目标 / 后处理 | 做不了光照、屏幕扭曲 | M22 需要时再加 `RenderPass` |
| 单一混合模式 | 预乘 alpha、加色做不了 | M06（分批断点已留好） |
| 资源全是占位 | 看不到真实画面 | 真实美术后置，见 [art-policy](architecture/art-policy.md) |
| 无解码器 | 现在放一个 PNG 进去也读不出来 | M05（注册 `IImageDecoder` 即可） |
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
| 传送点只是数据 | 没有切场景逻辑 | M25 消费 `portalAt` |
| 地图纹理绑定靠调用方 | JSON 的 `texture` 不会自动加载；composition root 必须 `setTileTexture` | M12 / M26 |
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
| 相机跟随未实现 | app 仍是固定相机；像素对齐平滑跟随待设计 | M16 / M22 / M25 |
| 玩家动作只记录不发布 | M03 事件化由组装根决定 | M16 |
| 内容加载不是事务 | 失败时会保留已解析的数据，便于修好后重试 | M15（多文件发布要小心） |
| 内容指针会失效 | 继续加载/热重载会让 `vector` 重分配 | M15（实体只存 `ContentId`） |
| 内容不校验资源存在性 | `AssetId` 只校验语法，catalog 没声明的纹理也能通过 | M05/M15 |
| 内容前缀规则很严 | `item.`/`crop.`/`recipe.`/`enemy.`/`loot.` 写错就报错 | M15（内容约定） |
| 内容表没有瓦片/商店/对话/任务 | M12 的 `TileDef` 仍是独立表；M23/M24 还要扩展 | M15/M12/M23/M24 |
| 内容版本不是内容哈希 | `contentVersion` 只是加载次数，跨存档兼容还要 M14 决定 | M15/M14 |
| 宽相 cell 尺寸固定 64 px | 大量超大碰撞体会推高 `maxCellOccupancy`，查询多捞 | M13 调优 |
| 无视锥/扇形查询 | AI 感知只能用 `overlap` + 自己过滤 | M21 |
| `Velocity` 是否进存档未定 | 速度是意图还是状态，会影响 M14 的格式 | M14 |
| 系统注册顺序是隐式耦合 | 顺序错了得到难查的 bug | 各玩法模块 + M27 |
| 场景与世界如何存档未定 | 存世界快照还是存场景描述 | M14 |
| 无场景过渡效果 | 切场景是硬切 | M25 |
| 无实体原型 → 世界规模 | 单场景 1e3 量级；10 万实体缓存局部性差 | M15 已实现；prefab 落地后再评估 |

## 6. 下一步建议

| 候选 | 理由 | 风险 |
|---|---|---|
| **M18 Inventory & Items**（推荐） | M15 ✅ 与 M09 ✅ 都已就绪：可以把物品定义、堆叠、装备、使用与背包网格接到 HUD/控件上，这是下一条能产生可见玩法的路径 | 堆叠/装备/掉落规则要一次定清；背包网格需要 M09 后续补布局组与滚动区 |
| M22 Calendar / Weather / Lighting | M19 的前置；M12 的 `MapAmbient` 已为光照/室内外准备；M09 已能显示日期/时间/季节 | 日历/季节/天气规则与光照叠加需要设计 |
| M10 Animation | 依赖 M05 ✅ + M06 ✅ 已就绪；M16 已经产出 `PlayerAnimationParams`，正好接上 | 剪辑/混合/状态机边界要一次想清楚 |
| M14 Serialization & Save | 依赖 M11 ✅；多槽 + 每游戏日 12 小时自动存档的需求已定 | 需要先决定「存世界快照还是存场景描述」 |
| M08 Audio | 独立性高，接口窄（`AudioCommand` 队列） | 需要首个新增音频第三方依赖，要先做一次 ADR |

选定后按既有节奏推进：**模块文档 → 实现 → 测试 → app 集成 → 三配置验证 → 全新克隆验证 → 回写文档 → 更新本文件 → 提交**。

## 7. 维护方式

每次交付完成后更新本文档的以下几处，这是「方便后续其他对话继续任务」的兑现方式：

1. §3 把模块从「未实现」移到「已实现」，填用例数与关键交付；
2. §3 的验收快照换成最新数字；
3. §5 增删跨模块风险；
4. §6 重排下一步建议；
5. 头部「最后更新」写上新的提交号（本次改动尚未提交，交付时补）。

历史交付顺序与当时的细节，看各模块的 `docs/build/mXX-*.md`；决策背后的一次性权衡，看 [ADR](adr/README.md)。
