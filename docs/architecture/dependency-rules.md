# 依赖规则

本文定义模块之间**允许与禁止**的依赖方式。目标是让引擎层与玩法层可以独立编译、独立测试、独立替换。

## 1. 总原则

1. **依赖只能指向下层**：L4 → L3 → L2 → L1 → L0；同层只能通过接口或事件通信。
2. **引擎不知道玩法**：`src/engine/` 不得 include `fa/game/...`。
3. **玩法不碰底层 API**：玩法层不得直接 include SDL3/OpenGL；通过 M02/M06/M07 的接口工作。
4. **只有一个 composition root**：`src/app/` 可以同时看到引擎与玩法的具体类型；其他模块只能看到接口。
5. **公共头不泄漏第三方类型**：公开头文件不得暴露 SDL/GL/ImGui 类型；第三方类型只出现在实现文件或适配层。
6. **数据向下，事件向上**：下层提供数据与能力；上层通过事件通知下层“发生了什么”，而不是让下层回调上层具体类。

## 2. 分层依赖矩阵

| 使用方 \ 被使用方 | L0 | L1 | L2 | L3 | L4 |
|---|---|---|---|---|---|
| L0 | ✅ 同层接口 | ❌ | ❌ | ❌ | ❌ |
| L1 | ✅ | ✅ 同层接口 | ❌ | ❌ | ❌ |
| L2 | ✅ | ✅ | ✅ 同层接口 | ❌ | ❌ |
| L3 | ✅ | ✅ | ✅ | ✅ 同层接口 | ❌ |
| L4 | ✅ | ✅ | ✅ | ✅ | ✅ 同层接口 |

“同层接口”指：依赖对方公开的抽象接口/事件/纯数据类型，而不是对方的完整实现。

## 3. 各层规则

### L0

- M00 只定义工程配置，不产生可链接代码。
- M01 不依赖 SDL、OpenGL、文件系统、窗口；只依赖 C++ 标准库与 glm。
- M02 是唯一直接 include SDL3 的模块；对外只暴露 `IWindow`、`IFileSystem`、`IClipboard` 等抽象。
- M03 的事件类型可以定义在 L0，但**具体玩法事件**必须定义在发布它的玩法模块。
- M04 只驱动阶段回调；不得 include 任何 L1+ 模块。

### L1

- M05 是资源访问唯一入口；其他模块不得直接 `std::ifstream` 读取游戏资源（平台配置文件除外）。对外提供 `AssetId`、`AssetHandle`、`IAssetManager`、`AssetCatalog`。
- M05 与 M06 **双向都不依赖**：M05 只经反向接口 `ITextureUploader` 拿到一个不透明 `std::uint32_t` token，由 composition root 适配成 `render::TextureHandle`。这条桥必须在 app 里接，不得为了让 M05 能建纹理而让它 include `fa/render/...`。
- M05 的资源缺失一律降级为色块占位并返回成功（见 [美术资源策略](art-policy.md)）；只有「id 未声明」「占位尺寸非法」「上传被拒」是错误。
- M06 是唯一直接调用 OpenGL 的模块（GL 上下文由 M02 的 `IGlContext` 提供）；对外提供 `IRenderer`、`RenderQueue`、`Camera2D`、`Viewport`、`TextureHandle`。批处理是实现细节，不是对外类型。
- M07 不包含玩法键位；只提供动作名到物理输入的映射。
- M08 不查询 ECS；只消费 `AudioCommand` 队列。
- M09 不依赖具体场景；通过 `UiBinding` 与回调工作。
- M10 不决定动画切换；只播放/混合调用方指定的动画。

### L2

- M11 是通用 ECS/场景容器；不得 include 任何 `fa/game/...`。
- M11 只依赖 M01（基础类型）、M04（`FrameContext`）、M06（`DrawItem`/`TextureHandle`/`RenderQueue`）。**不依赖 M05**：`Sprite` 只存裸 `TextureHandle`，纹理保活归加载方，避免把资源系统的异步加载与 `/EHsc` 拖进每个使用 ECS 的模块。
- M11 的提取接口产出**纯数据** `RenderQueue`，不接收 `IRenderer&`——这样渲染提取的测试不需要 GL 上下文。
- M12 只负责地图数据、图层与查询；耕地/作物规则属于 M19。
- M13 只负责几何与碰撞求解；伤害/交互判定属于 M20/M17。
- M14 只负责序列化机制与存档槽；具体组件的读写由组件/玩法模块注册。

### L3

- 玩法模块之间优先通过**组件 + 事件 + 窄接口**协作。
- 允许依赖“数据定义”头（如 `ContentId`、`ItemDef`），不允许依赖对方的系统实现类。
- 跨模块调用示例：战斗需要知道掉落物 → 发 `EntityDied` 事件，由掉落系统消费；而不是 `CombatSystem` 直接调用 `InventorySystem`。
- M25 可以协调多个玩法模块，但只通过它们的公开接口/事件；不得直接改对方内部状态。

### L4

- 调试/编辑器工具可以读取运行时数据，但写入必须通过受控接口。
- 发布构建必须能通过 `FA_ENABLE_EDITOR=0` / `FA_ENABLE_DEBUG_TOOLS=0` 完全剔除。
- 玩法不得依赖 M26/M27；调试代码通过接口注册，不能成为玩法逻辑的一部分。

## 4. 同层通信的三种方式

### 4.1 窄接口

```cpp
// fa/game/combat/ICombatQuery.h
class ICombatQuery {
public:
    virtual ~ICombatQuery() = default;
    virtual bool isAlive(EntityId id) const = 0;
    virtual int currentHealth(EntityId id) const = 0;
};
```

- 接口放在**使用方**或**独立 contract 头**中，实现方依赖接口，不反向依赖使用方。
- 接口方法尽量只读、少而稳定；避免把整个实现类暴露成接口。
- 接口对象生命周期由 composition root 管理；使用方不拥有接口。

### 4.2 事件

```cpp
struct CropHarvested {
    EntityId crop;
    ContentId item;
    int amount;
};
```

- 事件类型定义在**发布模块**的 `Events.h`；订阅者只 include 该头。
- 事件是值类型，携带自包含数据；不要携带指针或 `World&`。
- 两种投递方式：`publish<E>()` **立即**同步分发给当前订阅者；`enqueue<E>()` 入队，
  由拥有线程在帧末/阶段末调 `dispatchQueued()` 统一分发。后者的队列可跨线程写入，
  是工作线程把结果交回主线程的唯一通道。
- 订阅顺序按注册顺序，同一类型内稳定；跨类型不保证顺序。
- 事件不得用于每帧大量高频数据（如逐像素/逐瓦片）；高频数据用查询/共享缓冲。

### 4.3 纯数据契约

- `ContentId`、`EntityId`、`ItemStack`、`DamageInfo` 等放在低层 contract 头，双方都依赖数据，而不是彼此。
- 数据契约只含值类型与 `std::` 容器；不含行为、虚函数、指针。

## 5. Composition root 规则

`src/app/Application.cpp` 是唯一允许出现以下内容的地方：

- `#include <SDL3/SDL.h>`、`#include <glad/gl.h>`、`#include <imgui.h>`。
- 具体类构造：`SdlPlatform`、`GlRenderer`、`JsonContentDatabase`、`FarmScene`、`CombatScene`。
- 服务注册与系统注册顺序。
- 生命周期管理：创建顺序与销毁顺序相反。

其他模块只能通过构造函数参数、`ServiceRegistry` 或事件获取依赖。禁止在模块内部 `new` 一个具体实现并长期持有。

## 6. Include 规则

### 允许

```cpp
// 玩法模块依赖引擎公开接口
#include <fa/ecs/World.h>
#include <fa/content/ContentDatabase.h>
#include <fa/core/Result.h>
```

### 禁止

```cpp
// 引擎反向依赖玩法
#include <fa/game/farming/FarmSystem.h>

// 玩法直接使用底层 API
#include <SDL3/SDL.h>
#include <glad/gl.h>

// 公开头泄漏第三方类型
// fa/render/Renderer.h
#include <glm/glm.hpp> // 允许（纯数学值类型）
```

**例外**：`glm`、`std::`、`nlohmann/json` 的值类型可以在公开头中使用；SDL/GL/ImGui 类型必须在实现文件中使用，或封装为不透明句柄。

## 7. 编译期隔离

- 每个模块一个静态库 target（MSBuild `.vcxproj`），只链接它声明的依赖。
- 公共 include 目录按模块划分：`src/engine/core/include`、`src/engine/render/include` 等。
- 私有实现目录不加入其他项目的 include 路径。
- CI 增加“依赖扫描”脚本：检查 `src/engine/**` 是否出现 `fa/game/`、玩法是否出现 `SDL3/`/`glad/`。
- 目标名统一 `FA::Core`、`FA::Platform`、`FA::Render`、`FA::Game` 等，便于审查。

## 8. 循环依赖处理

出现 A ↔ B 时，按顺序尝试：

1. **下移共享数据**：把双方都需要的纯数据类型移到更低层 contract 头。
2. **提取接口**：由使用方定义窄接口，实现方依赖接口。
3. **改为事件**：A 发事件，B 订阅；避免直接调用。
4. **合并模块**：如果两者本质是同一职责，合并并更新模块地图。
5. **写 ADR**：记录为什么需要特殊依赖，以及如何限制影响范围。

禁止用 `forward declaration + 全局指针` 掩盖循环依赖。

## 9. 代码审查清单

- [ ] 新 include 是否指向同层或下层？
- [ ] 是否有引擎代码 include 了 `fa/game/...`？
- [ ] 玩法代码是否直接使用了 SDL/GL/ImGui？
- [ ] 公开头是否泄漏了第三方类型？
- [ ] 是否通过事件/接口而不是具体类通信？
- [ ] 新依赖是否已在模块文档与模块地图中登记？
- [ ] 是否引入了全局可变状态？
- [ ] 是否能在不启动窗口的情况下单元测试该模块？
