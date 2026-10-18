# 文档索引

Florian's Adventure 的全部设计文档入口。当前状态：设计方案已完成，**M00 构建骨架、M01 Core、M02 Platform、M03 Events & Jobs、M04 Time & Frame Loop、M05 Asset Pipeline、M06 Rendering、M07 Input、M09 UI & Text、M11 Scene / World & ECS、M12 Tilemap & World、M13 Physics & Collision、M15 Content Database、M16 Player Controller、M17 Interaction & Grid Placement 已实现并通过实测**，其余模块处于设计阶段。

> **要继续开发，先读 [计划完成情况](plan-status.md)** —— 它记录每个模块的状态、已有接口面、遗留风险与下一步建议，是新对话接手任务的唯一入口。

> 两条权威基线：产品与范围见 [ADR-0002](adr/0002-v1-scope-decisions.md)，像素与碰撞度量见 [ADR-0003](adr/0003-tile-and-collision-metrics.md) 与 [像素度量](architecture/pixel-metrics.md)。模块文档与它们冲突时，以 ADR 为准。

## 建议阅读顺序

0. [计划完成情况](plan-status.md) —— 什么已经做完、接口面在哪、下一步做什么。
1. [愿景与设计支柱](architecture/vision-and-pillars.md) —— 这个游戏是什么，不做什么。
2. [架构总览](architecture/overview-design.md) —— 分层、主循环、时间、坐标、ECS、线程与存档边界。
3. [模块地图](architecture/module-map.md) —— 28 个模块的职责与依赖图。
4. [依赖规则](architecture/dependency-rules.md) —— 什么能 include 什么。
5. [技术栈](architecture/tech-stack.md) —— 工具链、第三方库、构建与 CI。
6. [编码与内容规范](architecture/conventions.md) —— 命名、目录、错误处理、JSON 约定。
7. [里程碑](roadmap/milestones.md) 与 [垂直切片](roadmap/vertical-slice.md) —— 先做什么、怎么验收。
8. [模块设计文档](#模块设计文档) —— 需要实现某个模块时再读。
9. [架构决策记录](adr/README.md) —— 决策背后的权衡。

## 架构文档

| 文档 | 内容 |
|---|---|
| [愿景与设计支柱](architecture/vision-and-pillars.md) | 定位、核心循环、v1 范围、成功标准 |
| [架构总览](architecture/overview-design.md) | 分层、主循环、帧阶段、时间模型、坐标、ECS、资源、场景栈、线程、错误、存档 |
| [模块地图](architecture/module-map.md) | 28 模块总表、依赖图、层边界 |
| [依赖规则](architecture/dependency-rules.md) | 分层矩阵、同层通信、composition root、include 规则 |
| [技术栈](architecture/tech-stack.md) | C++20、编译选项、vcpkg、资源流水线、测试、CI |
| [编码与内容规范](architecture/conventions.md) | 命名、目录、错误处理、日志、ECS 约定、JSON 规范、Git 提交 |
| [美术资源策略](architecture/art-policy.md) | 色块占位规格、颜色约定、真实美术的接入方式 |
| [像素度量](architecture/pixel-metrics.md) | 虚拟分辨率、瓦片、精灵锚点、碰撞盒、排序与缩放 |

## 构建与实测

| 文档 | 内容 |
|---|---|
| [M00 构建骨架](build/m00-skeleton.md) | 目录布局、工程划分、vcpkg 与 CI 设计 |
| [M00 实测结果](build/m00-fixups.md) | 本机实测通过的配置、踩过的坑与遗留风险 |
| [M02 平台层・实测](build/m02-platform.md) | SDL3 接口语义、`/EHsc` 适用范围、静态库系统依赖、DPI 与原子写踩坑 |
| [M03 事件与作业・实测](build/m03-events-jobs.md) | 无 RTTI 类型键、分发快照语义、队列派发波次上限、`wait()` 全屏障与完成队列竞态 |
| [M04 帧循环・实测](build/m04-frame-loop.md) | 固定步累加、防死亡螺旋、暂停/缩放语义、浮点边界与断言路径踩坑 |
| [M05 资源管线・实测](build/m05-assets.md) | `friend` 与命名空间约束、占位降级语义、`/EHsc` 结论、异步等待用时间不用次数、错误码分段纠错 |
| [M06 渲染・实测](build/m06-render.md) | 自研 GL 函数表、`IGlContext` 归属、隐藏窗口上下文实测、整数缩放与排序语义 |
| [M07 输入・实测](build/m07-input.md) | `ActionId` 名字派生、上下文即独立 ActionMap、`asKeyboard()` 替代下行转型、`/EHsc` 结论修正 |
| [M09 UI 与文字・实测](build/m09-ui-and-text.md) | 内置位图字体、逐字节 ASCII、对齐用 advance、纹理句柄借用、UI 排序与纹理切换、无 `/EHsc` |
| [M11 场景与 ECS・实测](build/m11-scene-ecs.md) | `[[nodiscard]]` 与 C4834 连锁、`each` 需 const 重载、占位符句柄与代际溢出、测试不得读已销毁场景、统计快照时机 |
| [M12 瓦片地图・实测](build/m12-tilemap.md) | `Tilemap` 值类型与 `ISolidTiles` 适配器、walkable 反向逻辑、JSON 无异常读取、视锥裁剪的边界、自动瓦片快照 |
| [M13 物理与碰撞・实测](build/m13-physics-collision.md) | `std::unordered_map` 触发 C4530、负方向扫掠符号写反、半开区间吃掉边界、掩码必须双向、系统借用派生成员导致的悬空 |
| [M17 交互与格子放置・实测](build/m17-interaction-grid.md) | `payload` 取代内容 ID、`canInteract` 需要 actor、`cellsAlong` 的 NaN/负数、放置校验顺序、占用与碰撞两套权威 |
| [M16 玩家控制器・实测](build/m16-player-controller.md) | 输入适配与控制器拆分、只写 `Velocity`、动作锁、闪避速度平台、无敌窗口、测试顺序踩坑 |
| [M15 内容数据库・实测](build/m15-content-database.md) | ID 前缀/哈希、季节掩码、错误收集与重试、非事务加载、`std::span` 与指针稳定性、`/EHsc` 结论 |

## 已实现模块

| 模块 | 状态 | 实测（完成时累计） | 关键交付 |
|---|---|---|---|
| M01 Core | 已实现 | 22 用例 / 14626 断言 | `Result`/`Status`/`Error`、`Vector2`、`Rect`、`Rng`、断言与日志 |
| M02 Platform | 已实现 | 30 用例 / 14681 断言 | 窗口、事件泵、文件系统（含原子写）、单调时钟、消息框 |
| M03 Events & Jobs | 已实现 | 76 用例 / 16154 断言 | `EventBus` + RAII `Subscription`、`JobSystem` 线程池与 `parallelFor` |
| M04 Time & Frame Loop | 已实现 | 50 用例 / 14968 断言 | 固定步时钟、`FrameContext`、时间域暂停/缩放、`FrameLoop` |
| M05 Asset Pipeline | 已实现 | 215 用例 / 17092 断言 | `AssetId`/`AssetHandle`/`AssetCatalog`、异步加载 + 帧线程上传、色块占位降级、`ITextureUploader` 反向桥 |
| M06 Rendering | 已实现 | 154 用例 / 16780 断言 | `IRenderer` + `RenderQueue` + `Camera2D` + `Viewport`、自研 42 项 GL 函数表、精灵批处理与调试绘制 |
| M07 Input | 已实现 | 112 用例 / 16598 断言 | `ActionId` + `Binding` + 每上下文 `ActionMap`、上下文栈、`IInput` 窄接口、JSON 键位持久化 |
| M09 UI & Text | 已实现 | 406 用例 / 18532 断言 | 内置 3×5 ASCII 位图字体、字形回退、测量/换行/对齐、投影/描边、立即模式按钮/复选框/滑条/进度条、`RenderLayer::UI` 与主题 |
| M11 Scene / World & ECS | 已实现 | 293 用例 / 17378 断言 | `World` + `EntityId` 代际句柄 + `SparseSet` + `CommandBuffer`、`SystemPipeline` 三钩子调度、`SceneStack` 栈式场景、`SpriteExtractor` 纯数据提取 |
| M12 Tilemap & World | 已实现 | 350 用例 / 17952 断言 | `Tilemap` + `TileId`/`TileCoord`/`TileBounds` + 多图层 + 瓦片属性、世界/网格转换、区域与传送点、JSON 地图加载、4 邻接自动瓦片、`extractTiles` 视锥裁剪、`TilemapSolids` 物理适配器 |
| M13 Physics & Collision | 已实现 | 333 用例 / 17677 断言 | `Collider` + `CollisionLayer` 双向掩码、`SpatialHash` 宽相、`TileGrid` 静态碰撞、轴分离 + ≤4px 子步求解、`overlap`/`overlapCircle`/`raycast`、触发器进出事件 |
| M15 Content Database | 已实现 | 390 用例 / 18421 断言 | `ContentId` + `ItemDef`/`CropDef`/`RecipeDef`/`EnemyDef`/`LootTableDef`、季节掩码、JSON 多文档合并、前缀/重复/必填/数值校验、四类引用完整性、全量错误收集、按 ID/标签/类别查询 |
| M16 Player Controller | 已实现 | 379 用例 / 18243 断言 | `PlayerController` 走/跑/加速度/斜向归一化、朝向主导轴、状态机与动作锁、耐力/闪避/无敌窗口、`PlayerSystem` 输入适配、`PlayerActionRequest` 记录、动画参数 |
| M17 Interaction & Grid Placement | 已实现 | 365 用例 / 18136 断言 | `InteractionService` 目标选择（实体优先/朝向/距离/视线）、`Interactable` 组件、`IToolHandler` 分发、`PlacementGrid` 多格占用、`PlacementPreview`、交互/放置事件记录 |

各模块按 ID 排序，实际落地顺序为 M01 → M02 → M04 → M03 → M07 → M06 → M05 → M11 → M13 → M12 → M17 → M16 → M15 → M09，因此「完成时累计」栏不是单调递增的；
当前总计 **406 用例 / 18532 断言**。逐模块状态与下一步建议见 [计划完成情况](plan-status.md)。

三配置（Debug / Development / Release × x64）Rebuild 均零警告零错误。

## 路线图

| 文档 | 内容 |
|---|---|
| [里程碑](roadmap/milestones.md) | Phase A–F 的交付物、验收标准与风险 |
| [垂直切片](roadmap/vertical-slice.md) | 第一个可玩版本的范围、完成定义、验收脚本 |

## 架构决策记录

| 编号 | 标题 | 状态 |
|---|---|---|
| [ADR-0001](adr/0001-core-architecture.md) | 核心架构：分层、自研轻量 ECS、数据驱动、原生 MSBuild | Accepted |
| [ADR-0002](adr/0002-v1-scope-decisions.md) | v1 范围决策：耐力与闪避、季节与温室、加工链、多槽存档、键鼠输入 | Accepted |
| [ADR-0003](adr/0003-tile-and-collision-metrics.md) | 瓦片与碰撞度量：16×16 瓦片、640×360、脚部盒 10×6 | Accepted |
| [ADR-0004](adr/0004-core-math-and-result-shape.md) | Core 的值类型形态：`Rect` 独立结构体、`Result<T>` 无异常 | Accepted |
| [模板](adr/template.md) | 新 ADR 使用的模板 | — |

## 模块设计文档

### L0 基础设施

| ID | 文档 | 优先级 |
|---|---|---|
| M00 | [Build & Dependency Infrastructure](modules/00-build-infrastructure.md) | P0 |
| M01 | [Core Foundation](modules/01-core-foundation.md) | P0 |
| M02 | [Platform Layer](modules/02-platform-layer.md) | P0 |
| M03 | [Events & Jobs](modules/03-events-and-jobs.md) | P1 |
| M04 | [Time & Frame Loop](modules/04-time-and-frame-loop.md) | P0 |

### L1 引擎服务

| ID | 文档 | 优先级 |
|---|---|---|
| M05 | [Asset Pipeline](modules/05-asset-pipeline.md) | P0 |
| M06 | [Rendering](modules/06-rendering.md) | P0 |
| M07 | [Input](modules/07-input.md) | P0 |
| M08 | [Audio](modules/08-audio.md) | P1 |
| M09 | [UI & Text](modules/09-ui-and-text.md) | P1 |
| M10 | [Animation](modules/10-animation.md) | P1 |

### L2 世界运行时

| ID | 文档 | 优先级 |
|---|---|---|
| M11 | [Scene / World & ECS](modules/11-scene-world-ecs.md) | P0 |
| M12 | [Tilemap & World](modules/12-tilemap-and-world.md) | P1 |
| M13 | [Physics & Collision](modules/13-physics-collision.md) | P0 |
| M14 | [Serialization & Save](modules/14-serialization-save.md) | P1 |

### L3 玩法系统

| ID | 文档 | 优先级 |
|---|---|---|
| M15 | [Content Database](modules/15-content-database.md) | P0 |
| M16 | [Player Controller](modules/16-player-controller.md) | P1 |
| M17 | [Interaction & Grid Placement](modules/17-interaction-grid-placement.md) | P1 |
| M18 | [Inventory & Items](modules/18-inventory-items.md) | P1 |
| M19 | [Farming & World Objects](modules/19-farming-world-objects.md) | P1 |
| M20 | [Combat](modules/20-combat.md) | P1 |
| M21 | [AI](modules/21-ai.md) | P1 |
| M22 | [Calendar / Weather / Lighting](modules/22-calendar-weather-lighting.md) | P1 |
| M23 | [NPC / Dialogue / Quests](modules/23-npc-dialogue-quests.md) | P2 |
| M24 | [Economy / Crafting / Shops](modules/24-economy-crafting-shops.md) | P2 |
| M25 | [Game Flow](modules/25-game-flow.md) | P1 |

### L4 工具

| ID | 文档 | 优先级 |
|---|---|---|
| M26 | [Editor Tools](modules/26-editor-tools.md) | P2 |
| M27 | [Debug Tools](modules/27-debug-tools.md) | P1 |

## 状态标记

模块文档头部的“状态”字段取值：设计完成、部分设计、待重写、已实现、已废弃。

## 维护约定

- 新增模块：先更新 [模块地图](architecture/module-map.md)，再写模块文档，最后在此登记。
- 模块 ID 一旦分配不再复用。
- 改变架构决策：新增 ADR，同步修改架构文档与受影响模块文档。

## 关于 `docs/README.md` 与 `docs/architecture/overview.md`

这两个文件是早期脚手架留下的路径占位，保留它们是为了让外部链接不 404。内容已改为指向本文件与 [架构总览](architecture/overview-design.md) 的说明，请勿在其中编写正文。
