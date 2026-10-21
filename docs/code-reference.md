# 代码速查（Code Reference）

> **用途**：把 28 个模块的命名空间、工程依赖、公开头文件、类/结构体、关键方法、枚举与常量集中到一页，方便接手代码时快速定位。
>
> **来源**：`src/engine/include/fa/**/*.hpp`、各模块 `fa_*.vcxproj` 的 `ProjectReference`、[模块地图](architecture/module-map.md) 与各模块设计文档。
>
> **维护**：公开 API 变更时同步本文件；更细的语义、踩坑与限制仍以 [模块文档](index.md#模块设计文档)、[构建记录](index.md#构建与实测) 和 [已知限制](known-limitations.md) 为准。
>
> 符号约定：`kXxx` 为常量/错误码；`I*` 为接口；`*System`/`*Service` 为每帧或每步运行的服务；`*Def` 为内容定义；`*Stats` 为调试/冒烟快照。

## 0. 快速导航

| 模块 | 名称 | 工程 | 命名空间 | 设计文档 |
|---|---|---|---|---|
| M00 | Build & Dependency Infrastructure | 解决方案/脚本 | — | [00](modules/00-build-infrastructure.md) |
| M01 | Core Foundation | `fa_core` | `fa::core` | [01](modules/01-core-foundation.md) |
| M02 | Platform Layer | `fa_platform` | `fa::platform` | [02](modules/02-platform-layer.md) |
| M03 | Events & Jobs | `fa_events` | `fa::events` | [03](modules/03-events-and-jobs.md) |
| M04 | Time & Frame Loop | `fa_time` | `fa::time` | [04](modules/04-time-and-frame-loop.md) |
| M05 | Asset Pipeline | `fa_assets` | `fa::assets` | [05](modules/05-asset-pipeline.md) |
| M06 | Rendering | `fa_render` | `fa::render` | [06](modules/06-rendering.md) |
| M07 | Input | `fa_input` | `fa::input` | [07](modules/07-input.md) |
| M08 | Audio | `fa_audio` | `fa::audio` | [08](modules/08-audio.md) |
| M09 | UI & Text | `fa_ui` | `fa::ui` | [09](modules/09-ui-and-text.md) |
| M10 | Animation | `fa_anim` | `fa::anim` | [10](modules/10-animation.md) |
| M11 | Scene / World & ECS | `fa_scene` | `fa::scene` | [11](modules/11-scene-world-ecs.md) |
| M12 | Tilemap & World | `fa_tilemap` | `fa::tilemap` | [12](modules/12-tilemap-and-world.md) |
| M13 | Physics & Collision | `fa_physics` | `fa::physics` | [13](modules/13-physics-collision.md) |
| M14 | Serialization & Save | `fa_save` | `fa::save` | [14](modules/14-serialization-save.md) |
| M15 | Content Database | `fa_content` | `fa::content` | [15](modules/15-content-database.md) |
| M16 | Player Controller | `fa_player` | `fa::player` | [16](modules/16-player-controller.md) |
| M17 | Interaction & Grid Placement | `fa_interaction` | `fa::interaction` | [17](modules/17-interaction-grid-placement.md) |
| M18 | Inventory & Items | `fa_inventory` | `fa::inventory` | [18](modules/18-inventory-items.md) |
| M19 | Farming & World Objects | `fa_farming` | `fa::farming` | [19](modules/19-farming-world-objects.md) |
| M20 | Combat | `fa_combat` | `fa::combat` | [20](modules/20-combat.md) |
| M21 | AI | `fa_ai` | `fa::ai` | [21](modules/21-ai.md) |
| M22 | Calendar / Weather / Lighting | `fa_world` | `fa::world` | [22](modules/22-calendar-weather-lighting.md) |
| M23 | NPC / Dialogue / Quests | `fa_narrative` | `fa::narrative` | [23](modules/23-npc-dialogue-quests.md) |
| M24 | Economy / Crafting / Shops | `fa_economy` | `fa::economy` | [24](modules/24-economy-crafting-shops.md) |
| M25 | Game Flow | `fa_flow` | `fa::flow` | [25](modules/25-game-flow.md) |
| M26 | Editor Tools | `fa_editor` | `fa::editor` | [26](modules/26-editor-tools.md) |
| M27 | Debug Tools | `fa_debug` | `fa::debug` | [27](modules/27-debug-tools.md) |

## 1. 分层、命名空间与错误码

### 1.1 分层

```text
L0  fa::core  fa::platform
L1  fa::events  fa::time  fa::assets  fa::render  fa::input  fa::audio  fa::ui  fa::anim
L2  fa::scene  fa::tilemap  fa::physics  fa::save
L3  fa::content  fa::player  fa::interaction  fa::inventory  fa::farming
    fa::combat  fa::ai  fa::world  fa::narrative  fa::economy  fa::flow
L4  fa::editor  fa::debug
```

- 下层不 include 上层；跨层通信走纯值类型、事件或组装根注入的接口。
- 玩法模块不自己造 ECS：组件用 `scene::World::add<T>`，每帧逻辑写成 `scene::ISystem`。
- 位置只有一个写入者：`physics::PhysicsWorld::move` / `PhysicsSystem`；其他模块只写 `Velocity`。
- 调试/编辑器只在非 Release 链接；`fa_debug` 为叶子模块，命令由 app 注册。

### 1.2 命名空间一览

| 命名空间 | 主要子命名空间 | 归属 |
|---|---|---|
| `fa::core` | `core::log`、`core::error` | M01 |
| `fa::platform` | `platform::platform_error` | M02 |
| `fa::events` | — | M03 |
| `fa::time` | — | M04 |
| `fa::assets` | `assets::asset_error`、`assets::detail` | M05 |
| `fa::render` | `render::render_error` | M06 |
| `fa::input` | — | M07 |
| `fa::audio` | — | M08 |
| `fa::ui` | — | M09 |
| `fa::anim` | — | M10 |
| `fa::scene` | `scene::scene_error`、`scene::detail` | M11 |
| `fa::tilemap` | `tilemap::tilemap_error` | M12 |
| `fa::physics` | `physics::physics_error` | M13 |
| `fa::save` | `save::save_error` | M14 |
| `fa::content` | `content::content_error` | M15 |
| `fa::player` | `player::player_error` | M16 |
| `fa::interaction` | `interaction::interaction_error` | M17 |
| `fa::inventory` | `inventory::inventory_error` | M18 |
| `fa::farming` | `farming::farming_error` | M19 |
| `fa::combat` | — | M20 |
| `fa::ai` | — | M21 |
| `fa::world` | — | M22 |
| `fa::narrative` | `narrative::narrative_error` | M23 |
| `fa::economy` | `economy::economy_error` | M24 |
| `fa::flow` | `flow::flow_error` | M25 |
| `fa::editor` | — | M26 |
| `fa::debug` | — | M27 |

### 1.3 错误码分段

| 段 | 归属 | 备注 |
|---|---|---|
| 1000–1008 | `core::error` | 通用参数/IO/解析/断言 |
| 2000–2010 | `platform_error` | SDL/窗口/文件/GL/音频设备 |
| 3000–3007 | `asset_error` | 资源声明/加载/解码/上传 |
| 4000–4005 | `render_error` | GL 入口/着色器/纹理 |
| 5000–5007 | `scene_error` | 实体/组件/命令/场景栈 |
| 5008–5012 | `physics_error` | 碰撞体/瓦片/索引 |
| 5013–5022 | `tilemap_error` | 尺寸/图层/瓦片/JSON |
| 6000–6008 | `content_error` | 内容 JSON/ID/引用/季节 |
| 6100–6107 | `save_error` | 槽位/IO/版本/备份/迁移 |
| 7000–7012 | `interaction_error` | 目标/放置/占位 |
| 7013–7018 | `player_error` | 忙碌/冷却/耐力/方向/死亡 |
| 7019–7030 | `inventory_error` | 槽位/堆叠/转移/装备 |
| 7040–7053 | `farming_error` | 耕地/作物/种子/杂物 |
| 7200–7216 | `narrative_error` | NPC/对话/任务/条件 |
| 7300–7313 | `economy_error` | 商店/金币/加工/出货 |
| 7400–7410 | `flow_error` | 流程/槽位/存档/读档 |

## 2. 工程依赖（`ProjectReference`）

> 只列链接依赖；头文件里另有少量前向声明。完整规则见 [依赖规则](architecture/dependency-rules.md)。

| 工程 | 链接依赖 |
|---|---|
| `fa_core` | — |
| `fa_platform` | `fa_core` |
| `fa_events` | `fa_core` |
| `fa_time` | `fa_core`、`fa_platform` |
| `fa_assets` | `fa_core`、`fa_platform`、`fa_events` |
| `fa_render` | `fa_core`、`fa_platform` |
| `fa_input` | `fa_core`、`fa_platform` |
| `fa_audio` | `fa_core`、`fa_platform` |
| `fa_ui` | `fa_core`、`fa_render` |
| `fa_anim` | `fa_core`、`fa_render` |
| `fa_scene` | `fa_core`、`fa_time`、`fa_render` |
| `fa_tilemap` | `fa_core`、`fa_time`、`fa_render`、`fa_scene`、`fa_physics` |
| `fa_physics` | `fa_core`、`fa_time`、`fa_scene` |
| `fa_save` | `fa_core`、`fa_platform`、`fa_scene` |
| `fa_content` | `fa_core`、`fa_assets` |
| `fa_player` | `fa_core`、`fa_time`、`fa_input`、`fa_scene`、`fa_physics` |
| `fa_interaction` | `fa_core`、`fa_scene`、`fa_physics`、`fa_tilemap` |
| `fa_inventory` | `fa_core`、`fa_content` |
| `fa_farming` | `fa_core`、`fa_content`、`fa_inventory`、`fa_interaction`、`fa_scene`、`fa_tilemap` |
| `fa_combat` | `fa_core`、`fa_scene`、`fa_physics`、`fa_player` |
| `fa_ai` | `fa_core`、`fa_scene`、`fa_physics`、`fa_tilemap`、`fa_combat`、`fa_player` |
| `fa_world` | `fa_core` |
| `fa_narrative` | `fa_core`、`fa_content`、`fa_inventory`、`fa_scene` |
| `fa_economy` | `fa_core`、`fa_content`、`fa_inventory` |
| `fa_flow` | `fa_core`、`fa_save`、`fa_world`、`fa_economy`、`fa_narrative` |
| `fa_editor` | `fa_core`、`fa_tilemap`、`fa_render`、`fa_ui` |
| `fa_debug` | `fa_core`、`fa_platform`、`fa_render`、`fa_scene`、`fa_physics`、`fa_ui` |
| `fa_app` | 全部引擎库；`fa_debug`/`fa_editor` 仅非 Release |
| `fa_tests` | 全部引擎库（含 `fa_debug`，保证 Release 也能测） |

## 3. 模块速查

### M00 Build & Dependency Infrastructure

- **工程**：`FloriansAdventure.sln`、`build/Project.props`、`build/Sdl3.props`、`Directory.Build.props`、`vcpkg.json`、`scripts/build.ps1`、`.github/workflows/ci.yml`。
- **命名空间**：无（构建与脚本层）。
- **关键约定**：
  - 每个引擎模块一个静态库；`src/app` 是唯一可执行目标。
  - `/W4 /WX`、C++20、`/utf-8`、`/Zc:__cplusplus`、`/Zc:preprocessor`、默认 `/EHsc-`。
  - 需要 `/EHsc`：平台/JSON/`<chrono>`/`<unordered_map>` 的工程（见 [plan-status §2](plan-status.md)）。
  - 三配置：Debug / Development / Release × x64；Release 不链接 `fa_editor`/`fa_debug`。

### M01 Core Foundation

- **工程/依赖**：`fa_core`，无链接依赖。
- **命名空间**：`fa::core`、`fa::core::log`、`fa::core::error`。
- **头文件**：`Assert.hpp`、`Error.hpp`、`Hash.hpp`、`Ids.hpp`、`Log.hpp`、`Math.hpp`、`Result.hpp`、`Rng.hpp`、`TimeUnits.hpp`、`Version.hpp`。

| 类型 | 关键公开 API |
|---|---|
| `Error { int code; std::string message; }` | 值类型；错误码见 `error::*` |
| `Result<T>` / `Status` / `Unit` | `ok`、`err`、`isOk`、`isError`、`value`、`take`、`error`、`valueOr`、`ignoreValue`；`okStatus`、`errorStatus` |
| `Vec2` / `Color` / `Rect` | GLM 别名；`makeRect`、`isEmpty`、`center`、`left/right/top/bottom`、`intersect`、`clampf`、`clamp01`、`snapToPixel` |
| `Rng` | `seed`、`state`、`increment`、`restore`、`nextU32`、`bounded`、`range`、`nextFloat01`、`nextFloatRange`、`chance`、`nextIndex` |
| `StrongId<Tag>` | `value`、`isValid` |
| `ILogSink` | `write`、`flush`；`addLogSink`、`removeLogSink`、`log`、`logf`、`setLogLevel`、`logLevel`、`isLogEnabled`、`flushLogs` |
| `Seconds` / `Milliseconds` / `Countdown` | `toMilliseconds`、`toSeconds`、`start`、`stop`、`isRunning`、`remaining`、`progress`、`tick` |

- **枚举**：`LogLevel { Trace, Debug, Info, Warn, Error, Fatal, Off }`、`LogCategory { Core, Platform, Time, Asset, Render, Input, Audio, Ui, Animation, Ecs, Tilemap, Physics, Save, Content, Gameplay, Ai, Economy, Editor, Events, Count }`。
- **常量**：`kPi`、`kTau`、`Rng::kDefaultSeed`、`Rng::kDefaultStream`；错误码 `kUnknown`…`kAssertionFailed`（1000–1008）。
- **函数**：`hashBytes`、`engineVersion`、`setFatalHandler`、`fatal`、`reportCheckFailure`。

### M02 Platform Layer

- **工程/依赖**：`fa_platform` ← `fa_core`。
- **命名空间**：`fa::platform`、`fa::platform::platform_error`。
- **头文件**：`Platform.hpp`、`Window.hpp`、`FileSystem.hpp`、`GlContext.hpp`、`AudioDevice.hpp`、`PlatformEvent.hpp`、`Scancode.hpp`、`PlatformError.hpp`。

| 类型 | 关键公开 API |
|---|---|
| `IPlatform` | `initialize`、`shutdown`、`pumpEvents`、`quitRequested`、`window`、`files`、`glContext`、`audio`、`monotonicSeconds`、`sleepSeconds`、`showMessageBox` |
| `IWindow` | `setTitle`、`resize`、`setFullscreen`、`width`、`height`、`dpiScale`、`isFullscreen`、`nativeHandle` |
| `IFileSystem` | `exists`、`isDirectory`、`readText`、`readBinary`、`writeText`、`writeTextAtomic`、`createDirectories`、`listDirectory`、`remove` |
| `IGlContext` | `createContext`、`destroyContext`、`hasContext`、`makeCurrent`、`swapBuffers`、`setSwapInterval`、`swapInterval`、`procAddress` |
| `IAudioDevice` | `initialize`、`shutdown`、`isOpen`、`sampleRate`、`channels` |
| `PlatformEvent` | `type`、`scancode`、`keycode`、`repeat`、`button`、`x/y`、`dx/dy`、`width/height`、`text` |
| `WindowDesc` / `GlContextDesc` | 窗口与 GL 创建参数；`WindowDesc` 含 `minWidth`/`minHeight`/`resizable` |

- **枚举**：`PlatformEventType`、`MouseButton`、`Scancode`、`MessageBoxKind`。
- **常量**：`kScancodeCount`；错误码 2000–2010。
- **工厂**：`createPlatform()`；`AudioCallback = void(*)(void*, int16_t*, int)`。
- **要点**：`writeTextAtomic` 先写 `.tmp` 再改名；GL 上下文跟随窗口生命周期。

### M03 Events & Jobs

- **工程/依赖**：`fa_events` ← `fa_core`。
- **命名空间**：`fa::events`。
- **头文件**：`EventBus.hpp`、`JobSystem.hpp`。

| 类型 | 关键公开 API |
|---|---|
| `EventBus` | `subscribe`、`unsubscribe`、`publish`、`enqueue`、`dispatchQueued`、`clear`、`queuedCount`、`subscriberCount`、`totalSubscriberCount` |
| `Subscription` | RAII；`isValid`、`id`、`reset`、`release` |
| `JobSystem` | `submit`、`wait`、`parallelFor`、`parallelForErased`、`onComplete`、`drainCompletedJobs`、`workerCount`、`queuedJobCount` |
| `JobHandle` / `JobDesc` | `isValid`、`isComplete`；`std::function<void()>` 任务描述 |

- **别名**：`SubscriptionId`、`EventTypeId`、`Handler<E>`。
- **常量**：`kInvalidSubscriptionId`、`kMaxDispatchWaves`、`kAutoWorkerCount`、`kMaxWorkerCount`。
- **要点**：发布先拷贝订阅者快照；派发波次上限防止订阅中再发布导致死循环。

### M04 Time & Frame Loop

- **工程/依赖**：`fa_time` ← `fa_core`、`fa_platform`。
- **命名空间**：`fa::time`。
- **头文件**：`FrameLoop.hpp`、`FixedStepClock.hpp`、`FrameContext.hpp`、`TimeService.hpp`、`TimeDomain.hpp`。

| 类型 | 关键公开 API |
|---|---|
| `FixedStepClock` | `reset`、`advance`、`fixedStepSeconds`、`maxStepsPerFrame`、`maxRealDeltaSeconds`、`pendingSeconds`、`interpolationAlpha`、`fixedStepIndex`、`simulatedSeconds`、`droppedSteps` |
| `FrameContext` | `frameIndex`、`fixedStepIndex`、`realDeltaSeconds`、`scaledDeltaSeconds`、`fixedDeltaSeconds`、`interpolationAlpha`、`gameplayScale`、`fixedStepsThisFrame`、`isPaused` |
| `IFrameSink` | `onEvents`、`onFixedUpdate`、`onVariableUpdate`、`onRender` |
| `IFrameLoop` / `FrameLoop` | `run`、`requestQuit`、`time`、`timeService`、`clock`、`frameCount` |
| `ITimeService` / `TimeService` | `setPaused`、`isPaused`、`setScale`、`scale`、`effectiveScale`、`deltaFor`、`context` |

- **枚举**：`TimeDomain { Gameplay, Ui, Cinematic, Count }`。
- **常量**：`kFixedStepsPerSecond`、`kFixedStepSeconds`、`kMaxCatchUpSteps`、`kMaxRealDeltaSeconds`、`kMaxTimeScale`、`kTimeDomainCount`。
- **帧顺序**：pump events → `onEvents` → advance clock → `onFixedUpdate × N` → `onVariableUpdate` → `onRender`。

### M05 Asset Pipeline

- **工程/依赖**：`fa_assets` ← `fa_core`、`fa_platform`、`fa_events`。
- **命名空间**：`fa::assets`、`fa::assets::asset_error`、`fa::assets::detail`。
- **头文件**：`AssetId.hpp`、`AssetHandle.hpp`、`AssetCatalog.hpp`、`AssetManager.hpp`、`AssetTypes.hpp`、`ImageData.hpp`、`IImageDecoder.hpp`、`ITextureUploader.hpp`、`AssetError.hpp`。

| 类型 | 关键公开 API |
|---|---|
| `AssetId` | `fromName`、`name`、`hash`、`isValid` |
| `AssetCatalog` | `loadFromText`、`loadFromFile`、`clear`、`find`、`entries`、`size`、`empty` |
| `IAssetManager` / `AssetManager` | `loadTexture`、`requestTexture`、`loadJson`、`reload`、`update`、`purgeUnreferenced`、`releaseGpuTextures`、`releaseAll`、`stats`、`catalog` |
| `AssetHandle<T>` | `get`、`isValid`、`state`、`id`、`useCount`、`reset`、`swap` |
| `TextureAsset` / `JsonAsset` / `AssetStats` | 像素/GPU 句柄、JSON 文本、缓存统计 |
| `ImageData` | `rowPitch`、`requiredBytes`、`isValid` |
| `IImageDecoder` / `ITextureUploader` | `supports`、`decode`；`upload`、`release` |

- **枚举**：`AssetState { Unloaded, Loading, Ready, Failed, Count }`、`AssetKind { Texture, JsonText, Count }`、`ImageFormat { Rgba8, R8, Count }`。
- **工厂/工具**：`createAssetManager(AssetManagerDesc)`、`makeSolidImage`、`makeCheckerboardImage`、`parseColorText`、`toColorText`。
- **错误码**：3000–3007。
- **要点**：缺文件/解码失败降级为 catalog 占位纹理；JSON 失败直接报错；`update()` 在 GL 线程完成上传。

### M06 Rendering

- **工程/依赖**：`fa_render` ← `fa_core`、`fa_platform`。
- **命名空间**：`fa::render`、`fa::render::render_error`。
- **头文件**：`IRenderer.hpp`、`RenderQueue.hpp`、`Camera2D.hpp`、`Viewport.hpp`、`RenderTypes.hpp`、`RenderConstants.hpp`、`RenderError.hpp`。

| 类型 | 关键公开 API |
|---|---|
| `IRenderer` | `initialize`、`shutdown`、`isReady`、`createTexture`、`destroyTexture`、`textureSize`、`liveTextureCount`、`beginFrame`、`submit`、`submitAll`、`endFrame`、`drawDebugRect`、`drawDebugLine`、`stats`、`viewport`、`glVersion`、`glRenderer` |
| `RenderQueue` | `clear`、`submit`、`sort`、`items`、`size` |
| `Camera2D` | `position`、`setPosition`、`move`、`viewportSize`、`setViewportSize`、`setBounds`、`clearBounds`、`setFollowConfig`、`followConfig`、`follow`、`visibleRect`、`worldToViewport`、`viewportToWorld`、`worldToPixel`、`snapped`、`projection` |
| `CameraFollowConfig` | `deadzone`、`smoothing`、`maxSpeed` |
| `Viewport` | `fit`、`drawableWidth/Height`、`isEmpty`、`windowRect`、`windowToVirtual`、`virtualToWindow` |
| `DrawItem` | `texture`、`position`、`size`、`source`、`tint`、`rotation`、`flipX/Y`、`layer`、`sortY`、`entityId` |
| `TextureHandle` / `TextureDesc` / `RenderStats` | 纹理句柄/描述/帧统计 |

- **枚举**：`RenderLayer`（Ground/Objects/Actors/Weather/UI 等）、`TextureFormat`。
- **常量**：`kVirtualWidth=640`、`kVirtualHeight=360`、`kTileSize=16`、`kDefaultMaxQuads=4096`、`kMaxTextures=4096`、`kMaxTextureSize=16384`。
- **工厂/工具**：`createRenderer`、`depthKey`、`drawsBefore`、`countDrawCalls`、`countTextureSwitches`、`bytesPerPixel`。
- **错误码**：4000–4005。
- **跟随**：`follow(target, dt)` 死区内不动，离开后指数平滑（`smoothing` 1/s），可选 `maxSpeed`；每步仍 `setBounds` 夹取。

### M07 Input

- **工程/依赖**：`fa_input` ← `fa_core`、`fa_platform`。
- **命名空间**：`fa::input`。
- **头文件**：`Input.hpp`、`InputDevice.hpp`、`Action.hpp`、`ActionMap.hpp`、`Binding.hpp`、`InputContext.hpp`。

| 类型 | 关键公开 API |
|---|---|
| `IInput` | `isDown`、`wasPressed`、`wasReleased`、`axis`、`moveAxis`、`mousePosition`、`pushContext`、`popContext`、`endFrame` |
| `InputSystem` | `addDevice`、`deviceCount`、`hasDevice`、`keyboard`、`mouse`、`poll`、`actionState`、`mousePositionRaw`、`scroll`、`textInput`、`setMouseTransform`、`activeContext`、`contextStack`、`activeMap`、`mapFor`、`registerAction`、`actions`、`bind`、`unbind`、`applyDefaultBindings`、`reset`、`saveBindings`、`loadBindings`、`saveContextToJson`、`loadContextFromJson` |
| `KeyboardDevice` / `MouseDevice` | `poll`、`held`、`pressed`、`released`、`value`、`position`、`delta`、`scroll`、`text` |
| `ActionMap` | `bind`、`unbind`、`clearAction`、`clear`、`bindings`、`actionCount`、`actions` |
| `ActionRegistry` | `intern`、`name`、`nameView`、`size`、`clear` |
| `Binding` | `fromKey`、`fromMouseButton`、`toString`、`fromString` |
| `InputContextStack` | `push`、`pop`、`tryPop`、`top`、`contains`、`size`、`clear` |

- **枚举**：`InputContext { Gameplay, UI, Dialogue, Editor, TextInput, Count }`、`BindingKind { None, Key, MouseButton, MouseWheel, GamepadButton, GamepadAxis }`、`InputDeviceKind { Keyboard, Mouse, Gamepad }`。
- **函数/常量**：`actionIdFromName`、`isUsable`、`toString(InputContext)`、`inputContextFromName`、`kActionMoveLeft/Right/Up/Down`、`saveActionMapToJson`、`loadActionMapFromJson`。
- **帧顺序**：`poll(events)` → 读取动作 → `endFrame()`。

### M08 Audio

- **工程/依赖**：`fa_audio` ← `fa_core`、`fa_platform`。
- **命名空间**：`fa::audio`。
- **头文件**：`AudioEngine.hpp`、`AudioMixer.hpp`、`AudioClip.hpp`、`AudioTypes.hpp`、`SpscQueue.hpp`。

| 类型 | 关键公开 API |
|---|---|
| `AudioEngine` | `initialize`、`shutdown`、`isInitialized`、`play`、`stop`、`setBusVolume`、`setListener`、`update`、`stats` |
| `AudioMixer` | `setFormat`、`sampleRate`、`channels`、`add`、`stop`、`setBusVolume`、`busVolume`、`setListener`、`mix`、`reset`、`stats` |
| `AudioClip` | `isValid`、`frameCount`、`durationSeconds`；`makeTone`、`makeSilence` |
| `SoundDesc` / `AudioStats` | 播放参数（音量/pan/loop/fade/pitch）、运行统计 |
| `SpscQueue<T, Capacity>` | `tryPush`、`tryPop`、`size`、`empty`、`dropped`、`capacity` |

- **枚举**：`AudioBus { Master, SFX, Music, Ambience, Count }`。
- **常量**：`kInvalidSoundHandle`、`kMaxVoices`。
- **要点**：32 voice 软件混音；命令队列无锁 SPSC；设备打开失败降级为无音频。

### M09 UI & Text

- **工程/依赖**：`fa_ui` ← `fa_core`、`fa_render`。
- **命名空间**：`fa::ui`。
- **头文件**：`Font.hpp`、`Text.hpp`、`Ui.hpp`。

| 类型 | 关键公开 API |
|---|---|
| `FontAtlas` / `Glyph` | 内置 3×5 ASCII 图集：`glyphs`、`width`、`height`；`buildBuiltinFont`、`glyphFor` |
| `TextStyle` / `TextMetrics` | 颜色、缩放、对齐、投影/描边；测量结果 |
| 文字函数 | `measureText`、`drawText` |
| `UiContext` | `setOrigin`、`origin`、`setTheme`、`theme`、`beginFrame`、`endFrame`、`panel`、`border`、`progressBar`、`text`、`button`、`checkbox`、`slider`、`submit`、`itemCount`、`font` |
| `UiId` / `UiInputState` / `UiResult` / `UiTheme` | 控件身份、鼠标快照、控件结果、主题色/缩放 |

- **枚举**：`TextAlign { Left, Center, Right }`。
- **要点**：立即模式、鼠标 only；控件矩形显式给出；`submit()` 把绘制项追加到 `RenderQueue`，`RenderLayer::UI` 保证最后画。

### M10 Animation

- **工程/依赖**：`fa_anim` ← `fa_core`、`fa_render`。
- **命名空间**：`fa::anim`。
- **头文件**：`AnimationClip.hpp`、`Animator.hpp`、`AnimationService.hpp`、`AnimationStateMachine.hpp`。

| 类型 | 关键公开 API |
|---|---|
| `AnimationClip` / `AnimationFrame` | `isValid`、`totalDuration`；帧源矩形、时长、事件名 |
| `Animator` | 纯数据：`clip`、`time`、`frameIndex`、`speed`、`playing`、`finished`、`eventPending`、`events` |
| `AnimationService` / `IAnimationService` | `play(animator, clip, restart)`、`stop(animator)`、`update(animator, dt)` |
| `AnimationStateMachine` | `addState`、`addTransition`、`setState`、`state`、`update`、`service` |
| `DirectionalClips` / `AnimationTransition` / `AnimationParams` | 四向 clip、条件转换、速度/方向/状态参数 |

- **枚举**：`Direction { Down, Up, Left, Right }`。
- **函数**：`directionFromVector`。
- **要点**：clip 由代码构建，尚无资源 loader；事件在进入帧时触发。

### M11 Scene / World & ECS

- **工程/依赖**：`fa_scene` ← `fa_core`、`fa_time`、`fa_render`。
- **命名空间**：`fa::scene`、`fa::scene::scene_error`、`fa::scene::detail`。
- **头文件**：`World.hpp`、`EntityId.hpp`、`SparseSet.hpp`、`Components.hpp`、`ComponentType.hpp`、`CommandBuffer.hpp`、`System.hpp`、`Scene.hpp`、`SceneStack.hpp`、`SpriteExtractor.hpp`、`SceneError.hpp`。

| 类型 | 关键公开 API |
|---|---|
| `World` | `create`、`destroy`、`isAlive`、`add<T>`、`tryGet<T>`、`has<T>`、`remove<T>`、`storage<T>`、`each<Ts...>`、`countWith<Ts...>` |
| `EntityId` | `index`、`generation`、`isValid`、`isPending`；`toString` |
| `SparseSet<T>` | `tryGet`、`has`、`remove`、`size`、`empty`、`entityAt`、`valueAt`、`clear` |
| `Transform` / `Sprite` | 位置/缩放/旋转；纹理、尺寸、源矩形、tint、层、sortY |
| `CommandBuffer` | `create`、`destroy`、`add`、`remove`、`apply`、`resolve`、`pendingCount`、`appliedTotal` |
| `ISystem` / `SystemPipeline` | `onFixedUpdate`、`onVariableUpdate`、`onRenderExtract`；`add`、`fixedUpdate`、`variableUpdate`、`renderExtract`、`commands`、`stats` |
| `IScene` / `BasicScene` / `SceneStack` | `onEnter/Exit/Pause/Resume`、`world`、`systems`；`push`、`pop`、`replace`、`top`、`at`、`find`、`clear` |
| `SpriteExtractSystem` | `onRenderExtract`、`lastStats` |

- **枚举**：`CommandKind { Create, Destroy, AddComponent, RemoveComponent }`。
- **常量**：`ComponentTypeId` 由 `componentTypeIdOf<T>()` 分配。
- **错误码**：5000–5007。
- **要点**：`EntityId` 的 index 0 保留为“无实体”；销毁会提升 generation，旧句柄不会命中新实体。

### M12 Tilemap & World

- **工程/依赖**：`fa_tilemap` ← `fa_core`、`fa_time`、`fa_render`、`fa_scene`、`fa_physics`。
- **命名空间**：`fa::tilemap`、`fa::tilemap::tilemap_error`。
- **头文件**：`Tilemap.hpp`、`TileId.hpp`、`TileDef.hpp`、`Autotile.hpp`、`TilemapLoader.hpp`、`TilemapWriter.hpp`、`TilemapExtract.hpp`、`TilemapError.hpp`。

| 类型 | 关键公开 API |
|---|---|
| `Tilemap` | `reset`、`width`、`height`、`tileSize`、`origin/setOrigin`、`outsideIsSolid/setOutsideIsSolid`、`addTileDef`、`defFor`、`setTile`、`tileAt`、`fillLayer`、`setBorder`、`isWalkable`、`isWater`、`isTillable`、`isSolidCell`、`queryArea`、`portalAt`、`worldToGrid`、`gridToWorld`、`tileRect`、`boundsRect`、`stats`、`setTileTexture` |
| `TilemapSolids` | `setTilemap`、`tilemap`、`tileSize`、`isSolid`、`columns`、`rows` |
| `TileId` / `TileCoord` / `TileBounds` | ID、格子坐标、区域（`isEmpty`、`contains`） |
| `TileDef` / `TileLayer` | 瓦片属性（walkable/solid/tillable/texture）；图层与渲染层 |
| `MapArea` / `MapPortal` / `MapAmbient` | 区域类型（Outdoor/Greenhouse）、传送点、环境光 |
| `AutotileRule` | `variantFor(mask)`；`neighborMask`、`bakeAutotiles` |
| `TilemapExtractSystem` | `onRenderExtract`、`setViewBounds`、`clearViewBounds`、`setOptions`、`lastStats` |

- **枚举**：`AreaKind { Outdoor, Greenhouse, Count }`、`NeighborBit { North, East, South, West }`。
- **函数**：`loadTilemapFromJson`、`saveTilemapToJson`、`extractTiles`、`neighborMask`、`bakeAutotiles`。
- **错误码**：5013–5022。
- **要点**：地图是全量内存；自动瓦片是烘焙的，编辑后需重烘焙；纹理绑定由调用方 `setTileTexture`。

### M13 Physics & Collision

- **工程/依赖**：`fa_physics` ← `fa_core`、`fa_time`、`fa_scene`。
- **命名空间**：`fa::physics`、`fa::physics::physics_error`。
- **头文件**：`PhysicsWorld.hpp`、`Collider.hpp`、`CollisionLayer.hpp`、`PhysicsSystem.hpp`、`TileCollision.hpp`、`SpatialHash.hpp`、`PhysicsError.hpp`。

| 类型 | 关键公开 API |
|---|---|
| `PhysicsWorld` | `setTiles`、`setCellSize`、`sync`、`move`、`step`、`overlap`、`overlapCircle`、`raycast`、`debugDraw`、`updateTriggers`、`triggerEvents`、`stats`、`clear` |
| `PhysicsSystem` | `onFixedUpdate`、`physics`、`triggerEvents`、`lastStats` |
| `Collider` | `offset`、`size`、`layer`、`mask`、`isTrigger`；`colliderRect`、`layersInteract`、`blocksMovement`、`kPlayerFeetCollider` |
| `CollisionLayer` / `CollisionMask` | `maskOf`、`matches`、`layersInteract` |
| `ISolidTiles` / `TileGrid` | `isSolid`、`columns`、`rows`、`tileSize`；`overlapsSolidTile` |
| `SpatialHash` | 宽相桶：`insert`、`query`、`clear`、`stats` |
| `DebugShape` / `TriggerEvent` | 调试矩形；`TriggerPhase { Began, Ended }` |

- **枚举**：`CollisionLayer { None, World, Player, Enemy, PlayerAttack, EnemyAttack, Interactable, Trigger, All }`、`TriggerPhase { Began, Ended }`。
- **常量**：`kPlayerFeetCollider`（10×6，offset `{3,18}`）。
- **错误码**：5008–5012。
- **要点**：轴分离 + ≤4px 子步；实体对实体用步首快照；位置只由物理写。

### M14 Serialization & Save

- **工程/依赖**：`fa_save` ← `fa_core`、`fa_platform`、`fa_scene`。
- **命名空间**：`fa::save`、`fa::save::save_error`。
- **头文件**：`SaveArchive.hpp`、`SaveService.hpp`、`SaveTypes.hpp`、`ComponentSerializer.hpp`、`SaveError.hpp`。

| 类型 | 关键公开 API |
|---|---|
| `SaveArchive` | `object`、`array`、`parse`、`has`、`child`、`keys`、`at`、`push`、`setChild`、`setBool/Int/Int64/Float/Double/String/Vec2/Rect`、对应 `get*`、`dump` |
| `ISaveService` / `SaveService` | `save`、`load`、`apply`、`listSlots`、`deleteSlot`、`registerMigration`、`slotPath`、`backupPath`、`currentVersion` |
| `SaveMetadata` / `SaveContext` / `SaveFile` | 槽位元数据；待写上下文（world/player/inventories/quests/economy）；读回文件 |
| `IComponentSerializer` / `ComponentSerializerRegistry` | 组件读写注册表 |

- **常量**：`kSaveVersion=1`、`kSaveMagic="FA_SAVE"`。
- **错误码**：6100–6107。
- **要点**：原子写（`.tmp` + rename）、`.bak` 恢复、迁移链；`SaveArchive` 隐藏 nlohmann；槽名规则 1..64、字母数字`_`/`-`。

### M15 Content Database

- **工程/依赖**：`fa_content` ← `fa_core`、`fa_assets`。
- **命名空间**：`fa::content`、`fa::content::content_error`。
- **头文件**：`ContentDatabase.hpp`、`ContentId.hpp`、`ContentTypes.hpp`、`ContentError.hpp`。

| 类型 | 关键公开 API |
|---|---|
| `ContentId` | `fromName`、`name`、`isValid`；点分小写命名约定 |
| `ContentDatabase` | `loadDocument`、`validate`、`item`、`crop`、`recipe`、`enemy`、`lootTable`、`items`、`crops`、`recipes`、`enemies`、`lootTables`、`itemsWithTag`、`itemsWithCategory`、`errors`、`stats`、`schemaVersion`、`contentVersion`、`clear` |
| `ItemDef` | `id`、`displayName`、`description`、`icon`、`maxStack`、`sellPrice`、`category`、`toolKind`、`tags` |
| `CropDef` | `id`、`seedItem`、`harvestItem`、`stageDays`、`stageSprites`、`seasons`、`regrow`、`regrowDays` |
| `RecipeDef` | `inputs`、`output`、`requiredMinutes`、`machineKind`、`seasons` |
| `EnemyDef` / `LootTableDef` / `LootEntry` | 敌人属性/掉落表/条目 |
| `ContentError` | `code`、`context`、`message` |

- **枚举**：`Season { Spring, Summer, Fall, Winter, Any }`。
- **函数**：`parseSeasonMask`、`seasonAllows`、`toString(Season)`。
- **错误码**：6000–6008。
- **要点**：失败加载保留已解析数据便于重试；只存 `ContentId`，不存 `ContentDef*`。

### M16 Player Controller

- **工程/依赖**：`fa_player` ← `fa_core`、`fa_time`、`fa_input`、`fa_scene`、`fa_physics`。
- **命名空间**：`fa::player`、`fa::player::player_error`。
- **头文件**：`PlayerController.hpp`、`PlayerSystem.hpp`、`PlayerTypes.hpp`、`PlayerError.hpp`。

| 类型 | 关键公开 API |
|---|---|
| `PlayerController` | `attach`、`update`、`isDodging`、`isInvulnerable`、`events`、`clearEvents`、`stats`、`animationParams`、`setTuning`、`tuning` |
| `PlayerSystem` | `onFixedUpdate`、`setScriptedMove`、`clearScriptedMove`、`controller` |
| `PlayerTag` / `FacingDir` / `PlayerStateComp` | 标记、八向朝向、状态与动作锁 |
| `Health` / `Stamina` / `Wallet` / `DodgeState` | 生命、耐力、金币、闪避计时/无敌窗口 |
| `PlayerTuning` | 走/跑速度、加减速、闪避距离/时长/冷却/消耗、动作锁、无敌窗口 |
| `PlayerIntent` | `move`、`run`、`useTool`、`attack`、`interact`、`dodge`、`openInventory`、`payload` |
| `PlayerActionRequest` / `PlayerStats` / `PlayerAnimationParams` | 动作记录、统计、动画参数 |

- **枚举**：`Facing { North, East, South, West, NorthEast, SouthEast, SouthWest, NorthWest, Count }`、`PlayerState { Idle, Move, UseTool, Attack, Hurt, Dead, Menu, Count }`、`PlayerActionKind { UseTool, Attack, Interact, OpenInventory, Dodge, ActionRejected, Count }`。
- **函数**：`facingFromVector`（45° 扇区）、`directionVector`、`isDiagonal`、`cardinalFacing`、`toString`。
- **错误码**：7013–7018。
- **要点**：控制器只写 `Velocity`；动作布尔是边沿；斜向归一化；八向朝向与四向交互/动画需折叠。

### M17 Interaction & Grid Placement

- **工程/依赖**：`fa_interaction` ← `fa_core`、`fa_scene`、`fa_physics`、`fa_tilemap`。
- **命名空间**：`fa::interaction`、`fa::interaction::interaction_error`。
- **头文件**：`InteractionService.hpp`、`InteractionTypes.hpp`、`Interactable.hpp`、`ToolHandler.hpp`、`PlacementGrid.hpp`、`InteractionError.hpp`。

| 类型 | 关键公开 API |
|---|---|
| `InteractionService` | `pickTarget`、`pickTargetInFront`、`canInteract`、`interact`、`addHandler`、`events`、`clearEvents` |
| `Interactable` | 局部区域、优先级、payload |
| `IToolHandler` / `ToolUseContext` | `handles(ToolKind)`、`use(context)` |
| `PlacementGrid` | `resize`、`columns`、`rows`、`contains`、`isOccupied`、`ownerAt`、`payloadAt`、`cellsAlong`、`footprintRect`、`canPlace`、`place`、`remove`、`occupiedCells`、`clear`；`kMaxDimension=4096` |
| `GridTarget` / `PlacementRequest` / `PlacementPreview` / `InteractionEvent` | 目标、放置请求、预览、事件 |

- **枚举**：`Facing { North, East, South, West, Count }`、`ToolKind { None, Hoe, WateringCan, Axe, Pickaxe, Sword, Seed, Harvest, Placeable, Count }`、`InteractionEventKind { InteractRequested, ToolUsed, PlacementRequested, PlacementDenied, RemovalRequested }`。
- **错误码**：7000–7012。
- **要点**：实体优先于瓦片；视线是单中心线；放置占用与实体碰撞是两套数据。

### M18 Inventory & Items

- **工程/依赖**：`fa_inventory` ← `fa_core`、`fa_content`。
- **命名空间**：`fa::inventory`、`fa::inventory::inventory_error`。
- **头文件**：`Inventory.hpp`、`ItemStack.hpp`、`Hotbar.hpp`、`Equipment.hpp`、`ItemUseHandler.hpp`、`ItemUseService.hpp`、`PlayerInventory.hpp`、`InventoryError.hpp`。

| 类型 | 关键公开 API |
|---|---|
| `ItemStack` | `id`、`count`、`durability`、`empty` |
| `Inventory` | `at`、`slots`、`isValidSlot`、`maxStackFor`、`add`、`addToSlot`、`remove`、`removeAt`、`takeAt`、`swap`、`split`、`merge`、`transferTo`、`setSlot`、`restore`、`spaceFor`、`countOf`、`totalItems`、`usedSlots`、`slotCount`、`events`、`clearEvents`、`stats` |
| `Hotbar` | `select`、`cycle`、`selected`、`selectedStack`、`consumeSelected`、`inventory`、`events`、`clearEvents` |
| `Equipment` | `equip`、`unequip`、`at`；槽位 Tool/Weapon |
| `IItemUseHandler` / `ItemUseContext` | `handles(ItemDef)`、`use(context)` |
| `ItemUseService` | `registerHandler`、`use`、`events`、`clearEvents` |
| `PlayerInventory` | `backpack`、`hotbar`、`equipment`、`moveToHotbar`、`moveToBackpack` |

- **枚举**：`EquipmentSlot { Tool, Weapon, Count }`、`InventoryEvent::Kind { Added, Removed, Merged, Swapped, Split, Restored, Changed }`、`ItemUseEventKind { Used, Failed }`。
- **错误码**：7019–7030。
- **要点**：内容驱动堆叠；移除是原子的；耐久只参与分栈；使用不会自动消耗（handler 自己 `removeAt`）。

### M19 Farming & World Objects

- **工程/依赖**：`fa_farming` ← `fa_core`、`fa_content`、`fa_inventory`、`fa_interaction`、`fa_scene`、`fa_tilemap`。
- **命名空间**：`fa::farming`、`fa::farming::farming_error`。
- **头文件**：`FarmingService.hpp`、`FarmingTypes.hpp`、`FarmingTools.hpp`、`FarmingWorld.hpp`。

| 类型 | 关键公开 API |
|---|---|
| `FarmingService` | `till`、`water`、`plant`、`harvest`、`onDayChanged`、`setCurrentSeason`、`cropForSeed`、`entryAt`、`tileAt`、`cropAt`、`entries`、`restore`、`stats`、`events`、`clear` |
| `FarmingDayContext` | 日期/季节/天气/生长倍率/自动浇水 |
| `FarmTile` / `CropState` / `FarmTileEntry` | 耕地、作物阶段/浇水/再生、按格条目 |
| `FarmingToolHandler` | 实现 M17 `IToolHandler`：Hoe/WateringCan/Seed/Harvest/Axe/Pickaxe；`setInventory` 可切到玩家快捷栏 |
| `WorldObjectLayer` / `DebrisLayer` | 箱子/熔炉等占用物、杂草/石头/树枝清理 |
| `FarmingStats` / `FarmingEvent` | 统计与事件 |

- **枚举**：`FarmingEventKind`、`WorldObjectKind { Chest, Sprinkler, Furnace, Workbench, Sign, Count }`、`DebrisKind { Weed, Stone, Twig, Count }`。
- **错误码**：7040–7053。
- **要点**：纯按格数据，不进 ECS；作物按天推进；温室绕过季节限制；收获写 M18 背包。

### M20 Combat

- **工程/依赖**：`fa_combat` ← `fa_core`、`fa_scene`、`fa_physics`、`fa_player`。
- **命名空间**：`fa::combat`。
- **头文件**：`CombatService.hpp`、`CombatTypes.hpp`。

| 类型 | 关键公开 API |
|---|---|
| `ICombatService` / `CombatService` | `applyDamage`、`spawnAttack`、`spawnProjectile`、`applyStatus`、`update`、`damageEvents`、`deathEvents`、`projectileEvents`、`statusEvents`、`clearEvents`、`stats` |
| `Damage` / `AttackHitbox` / `ProjectileDesc` / `Projectile` | 伤害、攻击窗口盒、投射物参数/组件 |
| `StatusEffect` / `CombatStatus` | 中毒/减速/燃烧 |
| `LootSource` | 死亡掉落表名 |
| `DamageEvent` / `DeathEvent` / `ProjectileEvent` / `StatusEvent` | 四类事件 |
| `CombatStats` | 攻击/命中/击杀/投射物/状态统计 |

- **枚举**：`DamageType { Physical, Fire, Ice, Poison, Count }`、`StatusKind { Poison, Slow, Burn, Count }`。
- **要点**：攻击窗口单实例去重；投射物每步 raycast+move；击退走 `PhysicsWorld::move`；死亡只发事件，不销毁/掷掉落。

### M21 AI

- **工程/依赖**：`fa_ai` ← `fa_core`、`fa_scene`、`fa_physics`、`fa_tilemap`、`fa_combat`、`fa_player`。
- **命名空间**：`fa::ai`。
- **头文件**：`AiService.hpp`、`AiTypes.hpp`。

| 类型 | 关键公开 API |
|---|---|
| `AiService` | `update`、`setAggro`、`findPath`、`hasLineOfSight`、`stats`、`pathNodeCount`、`clear`、`setCombatService` |
| `AiAgent` | `state`、`home`、`stateTime`、`attackCooldown`、`loseTimer`、`repathTimer`、`target`、`targetVisible`、`profile` |
| `AiTargetTag` | 标记可被感知的目标 |
| `BehaviorProfile` | 视野/攻击距离/速度/伤害/冷却/逃跑阈值/重寻路/感知间隔/巡逻 |
| `AiStats` | 感知/寻路/移动/攻击/状态切换统计 |

- **枚举**：`AiState { Idle, Patrol, Chase, Attack, Flee, Return, Count }`。
- **要点**：A* 4 邻接 + 视线平滑；感知节流；不依赖 M16，目标用 `AiTargetTag` 标记；移动只用 `PhysicsWorld::move`。

### M22 Calendar / Weather / Lighting

- **工程/依赖**：`fa_world` ← `fa_core`。
- **命名空间**：`fa::world`。
- **头文件**：`Calendar.hpp`、`Lighting.hpp`。

| 类型 | 关键公开 API |
|---|---|
| `WorldCalendar` | `advance`、`skipToNextDay`、`setPaused`、`paused`、`clock`、`date`、`season`、`timeOfDay`、`hour`、`minute`、`isNight`、`today`、`tomorrow`、`isRaining`、`isStormy`、`isSnowing`、`autoWatersCrops`、`cropGrowthMultiplier`、`ambientColor`、`ambientIntensity`、`ambientLight`、`events`、`clearEvents`、`stats`、`restore`、`setRealSecondsPerGameMinute`、`weatherState`、`restoreWeather` |
| `GameDate` / `WorldClock` | 年/季/日；时刻、暂停、真实秒/游戏分 |
| `CalendarEvent` / `CalendarStats` | Hour/Day/Season/Weather 事件与统计 |
| `PointLight` / `LightSet` / `AmbientLight` | 点光、光源集合、环境光采样 |

- **枚举**：`Season { Spring, Summer, Fall, Winter, Count }`、`Weather { Sunny, Rainy, Stormy, Snowy, Count }`、`CalendarEventKind`。
- **常量**：`kDaysPerSeason=28`、`kSeasonsPerYear=4`、`kHoursPerDay=24`、`kStartHour=6`、`kDefaultRealSecondsPerGameMinute=0.7`。
- **要点**：时间 0–24；跨日/季/年；天气按季节权重；RNG 状态可存档；环境光是近似暗化，不是乘法光照。

### M23 NPC / Dialogue / Quests

- **工程/依赖**：`fa_narrative` ← `fa_core`、`fa_content`、`fa_inventory`、`fa_scene`。
- **命名空间**：`fa::narrative`、`fa::narrative::narrative_error`。
- **头文件**：`NarrativeDatabase.hpp`、`NarrativeService.hpp`、`NarrativeTypes.hpp`。

| 类型 | 关键公开 API |
|---|---|
| `NarrativeDatabase` | `loadDocument`、`clear`、`npc`、`dialogue`、`quest`、`npcs`、`dialogues`、`quests`、`stats`、`schemaVersion` |
| `NarrativeService` | `startDialogue`、`dialogueActive`、`activeDialogue`、`currentNode`、`availableChoices`、`choose`、`advance`、`endDialogue`、`startQuest`、`completeQuest`、`reportEvent`、`questState`、`isQuestActive`、`isQuestCompleted`、`activeQuests`、`giveGift`、`affinity`、`onDayChanged`、`events`、`stats` |
| `DialogueDef` / `DialogueNode` / `DialogueChoice` | 对话图、节点、选项 |
| `DialogueCondition` / `DialogueAction` | 白名单条件/动作（7 种条件、6 种动作） |
| `NpcDef` / `QuestDef` / `QuestState` | NPC、任务定义、任务进度 |
| `NarrativeEvent` / `NarrativeStats` | 事件与统计 |

- **枚举**：`DialogueCondition::Kind`、`DialogueAction::Kind`、`QuestObjectiveKind { Collect, Deliver, Kill, Plant, Talk, Count }`、`NarrativeEventKind`。
- **错误码**：7200–7216。
- **要点**：条件不是脚本引擎；任务由事件驱动；没有存档序列化；`OpenShop` 只记录动作，M24 负责交易。

### M24 Economy / Crafting / Shops

- **工程/依赖**：`fa_economy` ← `fa_core`、`fa_content`、`fa_inventory`。
- **命名空间**：`fa::economy`、`fa::economy::economy_error`。
- **头文件**：`EconomyService.hpp`、`EconomyTypes.hpp`、`ShopDatabase.hpp`。

| 类型 | 关键公开 API |
|---|---|
| `EconomyService` | `setContentDatabase`、`setShopDatabase`、`setSeason`、`setGold`、`gold`、`addGold`、`buyPrice`、`sellPrice`、`buy`、`sell`、`startCraft`、`update`、`collectCraft`、`ship`、`onDayChanged`、`jobs`、`shipping`、`restoreShipping`、`restoreJobs`、`restoreRemaining`、`readyJobCount`、`remaining`、`events`、`stats`、`clear` |
| `ShopDatabase` | `loadDocument`、`clear`、`shop`、`shops`、`size` |
| `ShopDef` / `ShopEntry` | 商店、库存条目（价格/每日限购） |
| `ProcessingJob` | 配方/产物/工作站/剩余分钟/ready |
| `EconomyEvent` / `EconomyStats` | 经济事件与统计 |

- **枚举**：`EconomyEventKind { ItemBought, ItemSold, CraftStarted, CraftReady, CraftCollected, ItemShipped, ShippingSettled, DailyRefresh, Count }`。
- **错误码**：7300–7313。
- **要点**：交易先检查金币/容量/库存再修改；失败路径不改状态；金币由服务持有，尚未写回 `player::Wallet`；商店表未进 M15；app 用 `shipping`/`restore*` 把金币、出货清单、加工任务与每日余量写入 `SaveFile.economy`。

### M25 Game Flow

- **工程/依赖**：`fa_flow` ← `fa_core`、`fa_save`、`fa_world`、`fa_economy`、`fa_narrative`。
- **命名空间**：`fa::flow`、`fa::flow::flow_error`。
- **头文件**：`GameFlow.hpp`、`FlowTypes.hpp`、`Sleep.hpp`。

| 类型 | 关键公开 API |
|---|---|
| `IGameFlow` / `GameFlow` | `startNewGame`、`loadGame`、`saveGame`、`saveToSlot`、`requestPause`、`endDay`、`requestQuit`、`state`、`session`、`onDayChanged`、`enterMainMenu`、`enterDialogue`、`exitDialogue`、`requestGameOver`、`respawn`、`update`、`markDirty`、`setSaveBuilder`、`setLoadHandler`、`setPauseHandler`、`setEndOfDayHandler`、`setDayStartHour`、`setAutosaveEnabled`、`setAutosaveIntervalDays`、`setAutosaveOnQuit`、`events`、`stats`、`reset` |
| 回调 | `setNewGameHandler`、`setSaveBuilder`、`setLoadHandler`、`setPauseHandler`、`setEndOfDayHandler`；类型 `NewGameHandler`、`SaveBuilder`、`LoadHandler`、`PauseHandler`、`EndOfDayHandler` |
| `GameSession` | `slot`、`playSeconds`、`dirty`、`autosaveIntervalDays` |
| `FlowEvent` / `FlowStats` | 状态/存档/日终/死亡事件与统计 |
| `Sleep` | `sleepHoursUntil`、`sleepRecovery`；常量 `kWakeHour=8`、`kFullSleepHours=8`、`kMinSleepRecovery=0.01` |

- **枚举**：`AppState { Boot, MainMenu, Loading, Playing, Paused, Dialogue, GameOver, Quit, Count }`、`FlowEventKind`。
- **常量**：`kAutosaveSlot="autosave"`、`kManualSlotCount=3`、`manualSlotName(i)`。
- **错误码**：7400–7410。
- **要点**：`Playing` 以外状态暂停 M22 时钟；日终顺序“推进时钟 → M19 → M24 → M23 → 自动存档”；睡眠满 8 h 全恢复、不足按 `(h/8)²` 衰减（最低 1%）；世界重建/存档应用走回调。

### M26 Editor Tools

- **工程/依赖**：`fa_editor` ← `fa_core`、`fa_tilemap`、`fa_render`、`fa_ui`；Release 不链接。
- **命名空间**：`fa::editor`。
- **头文件**：`MapEditor.hpp`、`EditorHistory.hpp`、`EditorOverlay.hpp`、`EditorTypes.hpp`。

| 类型 | 关键公开 API |
|---|---|
| `MapEditor` | `setLayer`、`layer`、`setBrush`、`brush`、`setTool`、`tool`、`setHover`、`hover`、`applyPrimary`、`applySecondary`、`paint`、`erase`、`fillRect`、`floodFill`、`cancelRect`、`previewCells`、`map`、`history`、`mapText`、`stats` |
| `EditorHistory` | `record`、`undo`、`redo`、`canUndo`、`canRedo`、`undoDepth`、`redoDepth`、`capacity`、`dirty`、`markSaved`、`clear`、`commands`、`undos`、`redos` |
| `EditorOverlay` | `toggle`、`setActive`、`active`、`update`、`draw`、`setTool`、`setLayer`、`setBrush`、`undo`、`redo`、`hover`、`previewCells`、`mapText`、`stats` |
| `EditorStats` / `EditorFrameInput` | 工具/层/笔刷/undo/redo/命令数/脏标记；鼠标与按键 |

- **枚举**：`EditorTool { Brush, Rect, Fill, Erase, Picker, Count }`。
- **快捷键**：F2 开关；1–5 工具；`[`/`]` 图层；Z/Y 撤销/重做；S/L 保存/加载；Esc 退出。
- **要点**：直接改运行时 `Tilemap`；命令差分 undo/redo；L 只验证加载，不替换实时地图。

### M27 Debug Tools

- **工程/依赖**：`fa_debug` ← `fa_core`、`fa_platform`、`fa_render`、`fa_scene`、`fa_physics`、`fa_ui`；Release 不链接。
- **命名空间**：`fa::debug`。
- **头文件**：`DebugConsole.hpp`、`DebugCVar.hpp`、`DebugLog.hpp`、`DebugStats.hpp`、`DebugInspector.hpp`、`DebugOverlay.hpp`、`DebugTools.hpp`、`DebugDraw.hpp`。

| 类型 | 关键公开 API |
|---|---|
| `DebugConsole` | `registerCommand`、`execute`、`print`、`clear`、`lines`、`history`、`setOpen`、`open`、`input`、`setInput`、`appendInput`、`backspace`、`submitInput`、`historyPrevious/Next` |
| `CVarRegistry` | `registerBool/Int/Float/String`、`get`、`set`、`list`、`save`、`load` |
| `DebugLogSink` | `write`、`snapshot`、`clear`、`setMinimumLevel` |
| `DebugTools` | `console`、`cvars`、`log`、`setStats`、`stats`、`setStatsVisible`、`toggleStats`、`setLogVisible`、`toggleLog`、`setPhysicsVisible`、`togglePhysics`、`setConsoleOpen`、`handleEvent`、`draw`、`registerCommand`、`attachLogSink`、`detachLogSink` |
| `DebugOverlay` | `draw` |
| `DebugStats` | FPS/帧/实体/绘制/资源/物理/音频/地图；`DebugCameraInfo`；`DebugAiTargetInfo` 列表 |
| `DebugCameraInfo` | `valid`、`position`、`viewportSize`、`visible`、`followTarget`、`following`、`deadzoneX/Y`、`smoothing` |
| `DebugAiTargetInfo` | `agent`、`target`、`hasTarget`、`targetVisible`、`state`、`agentPosition`、`targetPosition` |
| `EntityInspection` / `DebugShape` | 实体快照；物理调试矩形 |

- **枚举**：`CVarType { Bool, Int, Float, String }`、`LogLevel` 复用 core。
- **函数**：`splitCommandLine`、`trimCommandLine`、`formatStats`、`formatLogLine`、`formatInspection`、`inspectWorld`、`drawPhysicsShapes`。
- **快捷键**：F3 统计、F4 日志、F5 物理、反引号控制台。
- **要点**：M27 不依赖玩法模块；相机/AI 目标是 app 填的纯数据快照；AI 文本最多 6 行；F3 同时画 agent/目标连线。

## 4. App 组装根（`src/app/main.cpp`）

| 类型 | 作用 |
|---|---|
| `AssetUploaderAdapter` | 把 M05 的 `ITextureUploader` 桥到 M06 `IRenderer` |
| `fa::app::GameConfig` | `config/game.json` → 窗口/UI 缩放/路径/玩法参数，缺失逐项回落 |
| `fa::app::StbImageDecoder` | 注册给 M05 的 PNG/JPG/TGA/BMP 解码器（独立 TU） |
| `EntitySpawn` / `parseEntitySpawns` | `data/entities.json` 的 bed/shipping/merchant/enemy/hazard 生成表 |
| `EventsAndJobsDemo` | 演示 M03 的 tick/upload 事件与作业完成 |
| `CountingToolHandler` | 记录一次 M17 工具调用（Sword），避免与 M19 冲突 |
| `ConsumableUseHandler` | 记录一次 M18 食物使用 |
| `DemoScene` | 构建地图（scripted 40×23；play 80×23 农场+林地）、实体/农田/传送/荆棘，注册 Player/Physics/Tilemap/SpriteExtract 系统，运行 M17/M19/M23 脚本 |
| `SmokeSink` | `IFrameSink` 实现：事件路由、固定步、日历推进、渲染与 HUD，持有全部服务 |
| 各 `*DemoStats` | 每个模块的冒烟摘要快照 |

**启动选项**：`--window` 显示窗口；`--frames N` 跑 N 帧；`--play` 进入可操作会话（种植 + 近战/掉落，跳过脚本演示）；`--play-test` 无头跑通种植闭环 + 击杀 dummy + 拾取掉落并返回失败码。

**play 运行时**：`LootDrop` 是 app-local 组件并写入 `world.loot`；`processPlayerActions()` 消费 UseTool/Attack/Interact（床/出货箱/商人），`I` 开背包面板；`processCombatEvents()` 掷掉落/销毁敌人，`collectLoot()` 在 14 px 内自动拾取；`checkPortal()` 做双向传送，`tickHazard()` 处理荆棘与晕倒复活。

**主循环顺序**（`SmokeSink`）：

1. `onEvents`：编辑器/对话/控制台先消费事件，其余转发给 M07。
2. `onFixedUpdate`：`stack.fixedUpdate`（Player → Physics → TilemapExtract → SpriteExtract）；随后动画/战斗/AI 推进与 M03 tick。
3. `onVariableUpdate`：M22 `calendar.advance` + 事件计数；场景 variable update。
4. `onRender`：M08 `update`、`renderFrame()`（M05 上传 → 相机跟随 → 提取 → HUD → 农田/天气 → 调试绘制）、M27 统计刷新。
5. 循环结束后：M14 存档 smoke、M25 流程 smoke、汇总打印、按依赖逆序 shutdown。

**相机**：`camera` 以 `demoScene->markerCenter()` 为目标；死区 48×32、平滑 10/s、边界为地图 `boundsRect()`；M27 统计面板显示相机与跟随目标。

## 5. 常用扩展配方

| 目标 | 做法 |
|---|---|
| 新增组件 | `struct MyComp { ... };` + `world.add<MyComp>(id, value)`；`world.each<MyComp>(...)` |
| 新增每帧逻辑 | 实现 `scene::ISystem`，在 `BasicScene::onEnter` 里 `systems().add(...)`；注意注册顺序 |
| 新增内容类型 | 在 `ContentDatabase::loadDocument` 增加数组与校验；实体只存 `ContentId` |
| 新增工具 | 实现 `interaction::IToolHandler::handles/use`，`InteractionService::addHandler` |
| 新增敌人行为 | 扩展 `ai::AiState` + `AiService` 状态机分支；感知/移动仍走现有接口 |
| 新增存档段 | 用 `SaveArchive` 在 `SaveContext` 的 world/player/inventories/quests/economy 下写子对象；需要组件级时注册 `IComponentSerializer` |
| 新增 UI | `UiContext::beginFrame` 后用 `panel/text/button/checkbox/slider`，最后 `submit(renderQueue)` |
| 新增调试命令 | `DebugTools::registerCommand({name, help, handler})`；玩法命令由 app 注册 |
| 新增调试指标 | 扩展 `DebugStats` 字段，在 app 的 `updateDebugStats()` 填充 |
| 相机跟随 | `camera.setBounds(map.boundsRect())`、`camera.setFollowConfig({deadzone, smoothing, maxSpeed})`、每帧 `camera.follow(target, dt)` |
| 八向朝向 | `player::facingFromVector(move, fallback)`；四向消费者用 `cardinalFacing()` |

## 6. 公开头文件清单

> 路径均相对于 `src/engine/include/fa/`。实现文件在 `src/engine/<module>/src/`。

| 模块 | 头文件 |
|---|---|
| M01 core | `core/Assert.hpp`、`core/Error.hpp`、`core/Hash.hpp`、`core/Ids.hpp`、`core/Log.hpp`、`core/Math.hpp`、`core/Result.hpp`、`core/Rng.hpp`、`core/TimeUnits.hpp`、`core/Version.hpp` |
| M02 platform | `platform/AudioDevice.hpp`、`platform/FileSystem.hpp`、`platform/GlContext.hpp`、`platform/Platform.hpp`、`platform/PlatformError.hpp`、`platform/PlatformEvent.hpp`、`platform/Scancode.hpp`、`platform/Window.hpp` |
| M03 events | `events/EventBus.hpp`、`events/JobSystem.hpp` |
| M04 time | `time/FixedStepClock.hpp`、`time/FrameContext.hpp`、`time/FrameLoop.hpp`、`time/TimeService.hpp`、`time/TimeDomain.hpp` |
| M05 assets | `assets/AssetCatalog.hpp`、`assets/AssetError.hpp`、`assets/AssetHandle.hpp`、`assets/AssetId.hpp`、`assets/AssetManager.hpp`、`assets/AssetTypes.hpp`、`assets/IImageDecoder.hpp`、`assets/ImageData.hpp`、`assets/ITextureUploader.hpp` |
| M06 render | `render/Camera2D.hpp`、`render/IRenderer.hpp`、`render/RenderConstants.hpp`、`render/RenderError.hpp`、`render/RenderQueue.hpp`、`render/RenderTypes.hpp`、`render/Viewport.hpp` |
| M07 input | `input/Action.hpp`、`input/ActionMap.hpp`、`input/Binding.hpp`、`input/Input.hpp`、`input/InputContext.hpp`、`input/InputDevice.hpp` |
| M08 audio | `audio/AudioClip.hpp`、`audio/AudioEngine.hpp`、`audio/AudioMixer.hpp`、`audio/AudioTypes.hpp`、`audio/SpscQueue.hpp` |
| M09 ui | `ui/Font.hpp`、`ui/Text.hpp`、`ui/Ui.hpp` |
| M10 anim | `anim/AnimationClip.hpp`、`anim/AnimationService.hpp`、`anim/AnimationStateMachine.hpp`、`anim/Animator.hpp` |
| M11 scene | `scene/CommandBuffer.hpp`、`scene/Components.hpp`、`scene/ComponentType.hpp`、`scene/EntityId.hpp`、`scene/Scene.hpp`、`scene/SceneError.hpp`、`scene/SceneStack.hpp`、`scene/SparseSet.hpp`、`scene/SpriteExtractor.hpp`、`scene/System.hpp`、`scene/World.hpp` |
| M12 tilemap | `tilemap/Autotile.hpp`、`tilemap/TileDef.hpp`、`tilemap/TileId.hpp`、`tilemap/Tilemap.hpp`、`tilemap/TilemapError.hpp`、`tilemap/TilemapExtract.hpp`、`tilemap/TilemapLoader.hpp`、`tilemap/TilemapWriter.hpp` |
| M13 physics | `physics/Collider.hpp`、`physics/CollisionLayer.hpp`、`physics/PhysicsError.hpp`、`physics/PhysicsSystem.hpp`、`physics/PhysicsWorld.hpp`、`physics/SpatialHash.hpp`、`physics/TileCollision.hpp` |
| M14 save | `save/ComponentSerializer.hpp`、`save/SaveArchive.hpp`、`save/SaveError.hpp`、`save/SaveService.hpp`、`save/SaveTypes.hpp` |
| M15 content | `content/ContentDatabase.hpp`、`content/ContentError.hpp`、`content/ContentId.hpp`、`content/ContentTypes.hpp` |
| M16 player | `player/PlayerController.hpp`、`player/PlayerError.hpp`、`player/PlayerSystem.hpp`、`player/PlayerTypes.hpp` |
| M17 interaction | `interaction/Interactable.hpp`、`interaction/InteractionError.hpp`、`interaction/InteractionService.hpp`、`interaction/InteractionTypes.hpp`、`interaction/PlacementGrid.hpp`、`interaction/ToolHandler.hpp` |
| M18 inventory | `inventory/Equipment.hpp`、`inventory/Hotbar.hpp`、`inventory/Inventory.hpp`、`inventory/InventoryError.hpp`、`inventory/ItemStack.hpp`、`inventory/ItemUseHandler.hpp`、`inventory/ItemUseService.hpp`、`inventory/PlayerInventory.hpp` |
| M19 farming | `farming/FarmingService.hpp`、`farming/FarmingTools.hpp`、`farming/FarmingTypes.hpp`、`farming/FarmingWorld.hpp` |
| M20 combat | `combat/CombatService.hpp`、`combat/CombatTypes.hpp` |
| M21 ai | `ai/AiService.hpp`、`ai/AiTypes.hpp` |
| M22 world | `world/Calendar.hpp`、`world/Lighting.hpp` |
| M23 narrative | `narrative/NarrativeDatabase.hpp`、`narrative/NarrativeService.hpp`、`narrative/NarrativeTypes.hpp` |
| M24 economy | `economy/EconomyService.hpp`、`economy/EconomyTypes.hpp`、`economy/ShopDatabase.hpp` |
| M25 flow | `flow/FlowTypes.hpp`、`flow/GameFlow.hpp`、`flow/Sleep.hpp` |
| M26 editor | `editor/EditorHistory.hpp`、`editor/EditorOverlay.hpp`、`editor/EditorTypes.hpp`、`editor/MapEditor.hpp` |
| M27 debug | `debug/DebugConsole.hpp`、`debug/DebugCVar.hpp`、`debug/DebugDraw.hpp`、`debug/DebugInspector.hpp`、`debug/DebugLog.hpp`、`debug/DebugOverlay.hpp`、`debug/DebugStats.hpp`、`debug/DebugTools.hpp` |

## 7. 阅读入口

- **先读**：[计划完成情况](plan-status.md) → 本文件 → 对应模块设计文档 → 构建记录。
- **架构**：[架构总览](architecture/overview-design.md)、[模块地图](architecture/module-map.md)、[依赖规则](architecture/dependency-rules.md)。
- **限制**：[已知限制与后续改进汇总](known-limitations.md)。
- **路线图**：[里程碑](roadmap/milestones.md)、[垂直切片](roadmap/vertical-slice.md)。
