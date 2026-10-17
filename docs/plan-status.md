# 计划完成情况

**这份文档是「继续任务」的唯一入口。** 新开一个对话继续开发时，先读这里，再读对应的模块文档与构建记录，不需要重新翻历史。

- 最后更新：M11 Scene / World & ECS 交付完成（`d4e2e3b` 实现 + 本次文档回写）
- 当前总体：**28 个模块中 9 个已实现**（M00–M07 + M11），其余处于设计完成、尚未编码
- 当前可运行内容：窗口 + 主循环 + 事件/作业 + 输入 + 渲染 + 资源缓存 + **ECS 世界与场景栈**；画面是**全部色块占位**，921 个色块来自真实实体，标记块由系统驱动移动，但还没有玩法

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
| vcpkg | `D:\CPPLibraries\vcpkg\vcpkg.exe`，**每次都要显式** `$env:VCPKG_ROOT` |
| triplet | `x64-windows-static-md` |
| 全解决方案构建 | `MSBuild.exe FloriansAdventure.sln /p:Configuration=X /p:Platform=x64 /m /nologo /v:m` |
| 单工程构建 | 必须带 `/p:SolutionDir="$PWD\"` |
| 二进制 | `build\bin\x64\<Config>\fa_tests.exe`、`fa_app.exe` |
| PowerShell 调可执行文件 | 必须用 `& ".\build\bin\x64\$cfg\fa_tests.exe"`，直接写路径会被当字符串 |

全局编译开关：`/W4 /WX`、`stdcpp20`、`ConformanceMode`、`SDLCheck`、`ExceptionHandling=false`、
`RuntimeTypeInfo=false`、`/utf-8 /Zc:__cplusplus /Zc:preprocessor`。

需要 `/EHsc` 的工程：`fa_platform`（文件系统）、`fa_events`（`<chrono>`）、`fa_input`（键位 JSON）、
`fa_assets`（catalog JSON）、`fa_tests`（Catch2）、`fa_app`（把上面全 include 了一遍）。
判据不是「模块高不高级」，而是 **include 图里有没有 `<chrono>` 或 nlohmann/json**。
`fa_render` 与 `fa_scene` 都不需要——`fa_scene` 只依赖 `fa/core`、`fa/time`、`fa/render` 的头。

## 3. 模块状态

### 已实现（9 个）

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
| M11 | Scene / World & ECS | 78 | `World` + `EntityId` 代际句柄 + `SparseSet` + `CommandBuffer`、`SystemPipeline` 三钩子调度、`SceneStack` 栈式场景、`SpriteExtractor` 纯数据提取 | [设计](modules/11-scene-world-ecs.md) · [实测](build/m11-scene-ecs.md) |

实际落地顺序是 M01 → M02 → M04 → M03 → M07 → M06 → M05 → M11，所以上表顺序与时间顺序不同。

M05 交付时还顺带清理了一处历史遗留：`fa_render.vcxproj` 里有一条指向 `fa_assets` 的 `ProjectReference`，但没有任何源文件用它，已删除。**M05 与 M06 现在编译期互不依赖**——上传走 `ITextureUploader` 反向接口，由 `fa_app` 适配。

M11 同样保持窄依赖：`fa_scene` 只引用 `fa_core`、`fa_time`、`fa_render`，**不引用 `fa_assets`**。
`Sprite` 只存裸 `render::TextureHandle`，纹理保活归加载方（asset manager 的生命周期长于任何场景），
所以使用 ECS 的模块不会被资源系统的 JSON 依赖与 `/EHsc` 连坐。提取接口产出纯数据 `RenderQueue`
而不是接收 `IRenderer&`，因此渲染提取的测试完全不需要 GL 上下文。

**验收快照**（三配置一致）：

```text
fa_tests.exe → All tests passed (17378 assertions in 293 test cases)
             "[scene]"   → 286 assertions in 78 test cases
             "~[scene]"  → 17092 assertions in 215 test cases
fa_app.exe --frames 60 → M02+M03+M04+M05+M06+M07+M11 smoke test OK
```

### 未实现（19 个）

| 模块 | 名称 | 优先级 | 依赖是否已就绪 | 备注 |
|---|---|---|---|---|
| M08 | Audio | P1 | 需要 M05 ✅ | 需要 SDL_mixer 或自研混音；首个真正的新增第三方依赖点 |
| M09 | UI & Text | P1 | 需要 M06 ✅ | 字体图集 + 字形缓存 + 排版；M06 明确把文字留给它 |
| M10 | Animation | P1 | 需要 M05 ✅、M06 ✅ | 剪辑/混合/状态机边界；`revision` 已为它准备 |
| M12 | Tilemap & World | P1 | 需要 M11 ✅ | 地图数据、图层、查询；瓦片纵向只有 22.5 格（见风险） |
| M13 | Physics & Collision | **P0** | 需要 M11 ✅ | 脚部盒 10×6；ADR-0003 已定度量 |
| M14 | Serialization & Save | P1 | 需要 M11 ✅ | 多槽存档、游戏时间每 12 小时自动存档 |
| M15 | Content Database | **P0** | 需要 M05 ✅ | 用 `loadJson` 读定义表；JSON 无占位是刻意设计 |
| M16 | Player Controller | P1 | 需要 M11、M13、M17 | 移动、工具使用、耐力 |
| M17 | Interaction & Grid Placement | P1 | 需要 M12、M13 | 格子放置、交互目标 |
| M18 | Inventory & Items | P1 | 需要 M15、M09 | |
| M19 | Farming & World Objects | P1 | 需要 M12、M17、M22 | 作物、耕地、温室 |
| M20 | Combat | P1 | 需要 M13、M16、M18 | 含耐力与闪避（ADR-0002） |
| M21 | AI | P1 | 需要 M11 ✅、M13 | |
| M22 | Calendar / Weather / Lighting | P1 | 需要 M04 ✅、M06 ✅ | 季节驱动 M19/M21 行为差异 |
| M23 | NPC / Dialogue / Quests | P2 | 需要 M09、M15 | |
| M24 | Economy / Crafting / Shops | P2 | 需要 M15、M18 | 星露谷式加工链 |
| M25 | Game Flow | P1 | 需要 M11 ✅、M14 | 场景栈、菜单流 |
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
| 实体与组件（**L2，玩法都用它**） | `World`、`EntityId`、`CommandBuffer`、`Transform`、`Sprite` | `fa/scene/` |
| 系统调度 | `ISystem`、`SystemPipeline`（`onFixedUpdate`/`onVariableUpdate`/`onRenderExtract`） | `fa/scene/System.hpp` |
| 场景 | `IScene`、`BasicScene`、`SceneStack` | `fa/scene/` |
| 世界 → 绘制列表 | `SpriteExtractSystem`、`extractSprites`、`SpriteExtractOptions` | `fa/scene/SpriteExtractor.hpp` |

错误码分段（`Error.hpp`，加新码前先看自己该在哪个段）：
1000–1999 Core、2000–2999 Platform、3000–3999 Assets、**4000–4999 Render/Input/Audio/UI**、
**5000–5999 Scene（已用 5000–5007）/ Physics**、6000–6999 Save/Content、7000–7999 Gameplay。

> 玩法模块（M15–M25）**不要**自己再造一套实体/组件管理：加组件就 `world.add<MyComponent>(id, ...)`，
> 每帧逻辑就写一个 `ISystem` 挂到 `systems().add(...)`。反向依赖（`fa_scene` include `fa/game/...`）是硬禁止。

## 5. 跨模块遗留风险

| 风险 | 影响 | 归属 |
|---|---|---|
| `kVirtualHeight = 360` 不是 16 的整数倍 | 纵向 22.5 格；地图布局要么按 22 行要么改 640×352 | M10/M12 |
| 旋转精灵破坏像素风 | `DrawItem::rotation` 只靠注释限制 90° 倍数 | M06（接受） |
| 无文字绘制 | 任何文字（含调试文字）都画不出来 | M09 |
| 无渲染目标 / 后处理 | 做不了光照、屏幕扭曲 | M22 需要时再加 `RenderPass` |
| 单一混合模式 | 预乘 alpha、加色做不了 | M06（分批断点已留好） |
| 资源全是占位 | 看不到真实画面 | 真实美术后置，见 [art-policy](architecture/art-policy.md) |
| 无解码器 | 现在放一个 PNG 进去也读不出来 | M05（注册 `IImageDecoder` 即可） |
| 隐藏窗口依赖驱动 | 无 GL 驱动的 CI 镜像拿不到上下文 | M06 已降级为非致命 |
| `/EHsc` 依赖第三方 include 图 | 新增 JSON/`<chrono>` 依赖会连锁要求 `/EHsc` | M00 规约 |
| ECS 无实体原型 / prefab | 建复杂实体要在代码里逐行 `add`；内容驱动的实体要等 M15 | M11（等 M15 补） |
| `Sprite` 只存裸 `TextureHandle` | 纹理被 asset manager 卸载后句柄悬空 | M11 + M05 热重载 |
| `SparseSet::remove` 打乱遍历顺序 | 依赖顺序的逻辑会偶发错乱；要顺序得自己排序 | 各玩法模块 |
| 无空间分区索引 | 邻近查询（碰撞、AI 感知）只能线性扫 | M13 / M21 |
| 系统注册顺序是隐式耦合 | 顺序错了得到难查的 bug | 各玩法模块 + M27 |
| 场景与世界如何存档未定 | 存世界快照还是存场景描述 | M14 |
| 无场景过渡效果 | 切场景是硬切 | M25 |
| 无实体原型 → 世界规模 | 单场景 1e3 量级；10 万实体缓存局部性差 | M12 落地后再评估 |

## 6. 下一步建议

| 候选 | 理由 | 风险 |
|---|---|---|
| **M13 Physics & Collision**（推荐） | 另一个还没落地的 P0 基础设施，依赖 M11 ✅ 已就绪；脚部盒 10×6 已由 [ADR-0003](adr/0003-tile-and-collision-metrics.md) 定死，能立刻让方块真的碰撞而不是穿模 | 移动求解的顺序（先 X 后 Y）与穿透是经典坑；瓦片查询要等 M12 或先做纯几何版 |
| M10 Animation | 依赖 M05 ✅ + M06 ✅ 已就绪，而且**现在有实体可以挂动画了**——M11 的 `Sprite` 正是它要改写的字段 | 剪辑/混合/状态机边界要一次想清楚；`revision` 机制已备好 |
| M09 UI & Text | 解锁「能看到字」——现在任何文字（含调试数字）都画不出来 | 字体图集 + 字形缓存 + 排版是独立工程 |
| M08 Audio | 独立性高，接口窄（`AudioCommand` 队列） | 需要首个新增音频第三方依赖，要先做一次 ADR（SDL_mixer vs 自研混音） |
| M12 Tilemap & World | 依赖 M11 ✅；能把 920 个手搓色块换成真实地图 | 与 M13 有交界（瓦片碰撞归谁）；建议 M13 先定几何接口 |
| M14 Serialization & Save | 依赖 M11 ✅；多槽 + 每游戏日 12 小时自动存档的需求已定 | 需要先决定「存世界快照还是存场景描述」，是设计题不是编码题 |

选定后按既有节奏推进：**模块文档 → 实现 → 测试 → app 集成 → 三配置验证 → 全新克隆验证 → 回写文档 → 更新本文件 → 提交**。

## 7. 维护方式

每次交付完成后更新本文档的以下几处，这是「方便后续其他对话继续任务」的兑现方式：

1. §3 把模块从「未实现」移到「已实现」，填用例数与关键交付；
2. §3 的验收快照换成最新数字；
3. §5 增删跨模块风险；
4. §6 重排下一步建议；
5. 头部「最后更新」写上新的提交号。

历史交付顺序与当时的细节，看各模块的 `docs/build/mXX-*.md`；决策背后的一次性权衡，看 [ADR](adr/README.md)。
