# M11 Scene / World & ECS

| 项 | 值 |
|---|---|
| 层 | L2 |
| 优先级 | P0 |
| 状态 | 已实现 |
| 依赖 | M00、M01、M04（时间）、M06（`DrawItem`/`TextureHandle`） |
| 被依赖 | M12–M27（几乎所有玩法模块） |
| 实现记录 | [M11 场景与 ECS：实现说明、实测结果与遗留风险](../build/m11-scene-ecs.md) |

## 1. 目的

提供玩法模块共享的**唯一可变状态容器**：实体-组件-系统世界、实体生命周期、系统调度、场景栈，以及把世界翻译成绘制列表的渲染提取。

在这之前，M05/M06/M07 各自都是能单独跑通的服务，但没有任何东西把它们串起来——渲染器接收的是 app 里手搓的 `DrawItem`。M11 落地后，画面上的每一块色块都来自一个真实的实体，移动来自一个真实的系统。

## 2. 范围

**负责**

- `EntityId`：索引 + 代际，防止悬空引用；销毁后旧句柄自动失效。
- 组件存储：`SparseSet<T>`，O(1) 增删查 + 紧凑遍历。
- `World`：创建/销毁实体（槽位回收 + 代际自增）、组件增删查、单/多组件视图迭代。
- `CommandBuffer`：延迟结构变更（创建/销毁/加组件/删组件），占位符句柄在 `apply()` 时解析。
- `SystemPipeline`：`onFixedUpdate`、`onVariableUpdate`、`onRenderExtract` 三类钩子，按注册顺序执行；每个系统之后自动刷新命令缓冲。
- `IScene` / `BasicScene` / `SceneStack`：场景生命周期、栈式叠加、下层暂停策略。
- `SpriteExtractor`：遍历 `Transform`+`Sprite` 生成 `render::RenderQueue` 条目，是 M11 与 M06 之间**唯一**的桥。
- `Transform`、`Sprite`：v1 的两个内置组件。

**不负责**

- 具体玩法系统（M16–M25）——它们定义自己的组件与系统。
- 瓦片地图数据与查询（M12）。
- 物理求解与碰撞（M13）。
- 动画播放（M10）：`Sprite` 只是「一张纹理画在哪」，不含剪辑与帧。
- 序列化（M14）：`World` 没有 `save`/`load`，世界快照由 M14 决定。
- 渲染实现（M06）：提取只产出纯数据，不触碰 GL。
- 实体原型 / prefab：v1 不实现，见 §9。

## 3. 交付的文件

### 公共头（`src/engine/include/fa/scene/`）

| 文件 | 内容 |
|---|---|
| `SceneError.hpp` | 错误码 5000–5007：`kNotInitialized`、`kInvalidEntity`、`kEntityNotAlive`、`kComponentMissing`、`kInvalidCommand`、`kNullScene`、`kSceneStackOverflow`、`kSceneNotOnStack` |
| `EntityId.hpp` | `EntityId{index, generation}`、`kPendingGeneration`、`isValid()`/`isPending()`、比较、`std::hash` 特化、`toString()` |
| `ComponentType.hpp` | `ComponentTypeId`、`kInvalidComponentType`、`componentTypeIdOf<T>()`、`componentSetIdOf<Ts...>()` |
| `SparseSet.hpp` | 模板 `SparseSet<T>`：`add`/`tryGet`/`has`/`remove`/`size`/`empty`/`sparseCapacity`/`clear`/`entityAt`/`valueAt` |
| `World.hpp` | `WorldStats`、`detail::StorageBase`/`Storage<T>`、`World`（`create`/`destroy`/`isAlive`/`add`/`tryGet`/`has`/`remove`/`storage`/`each`/`countWith`/`stats`/`clear`） |
| `Components.hpp` | `Transform`（位置/缩放/旋转）、`Sprite`（纹理/尺寸/源矩形/色调/图层/排序偏移/翻转/实体覆盖） |
| `CommandBuffer.hpp` | `CommandBufferStats`、`CommandKind`、`detail::CommandBase` 与四个命令、`CommandBuffer` |
| `System.hpp` | `ISystem`（名字 + 三钩子）、`SystemPipelineStats`、`SystemPipeline` |
| `Scene.hpp` | `IScene`（生命周期 + 三钩子）、`BasicScene`（持 `World` + `SystemPipeline`） |
| `SceneStack.hpp` | `SceneStackStats`、`SceneStack`（`kMaxDepth = 16`） |
| `SpriteExtractor.hpp` | `SpriteExtractOptions`、`SpriteExtractStats`、`extractSprites()`、`SpriteExtractSystem` |

### 私有实现（`src/engine/scene/src/`）

| 文件 | 内容 |
|---|---|
| `EntityId.cpp` | `toString()`：`e12v3` / `e0` / `e5pending` |
| `World.cpp` | `create`（空闲表复用 + 代际自增 + 溢出保护）、`destroy`（先清全部组件再入空闲表）、`isAlive`、`stats`、`clear` |
| `CommandBuffer.cpp` | `resolvePending`、四个命令的 `run`、`apply`（按 kind 计数）、`resolve`、`clear` |
| `System.cpp` | `add`（忽略空指针）、三段调度、`drain`（每个系统之后 `apply`） |
| `Scene.cpp` | `BasicScene` 把三个钩子转发给 pipeline |
| `SceneStack.cpp` | `push`（暂停下层 + `onEnter`）、`pop`（`onExit` + 恢复下层）、`replace`、`top`/`at`/`find`、`clear`、三段驱动 |
| `SpriteExtractor.cpp` | `extractSprites`（`world.each<Transform, Sprite>`）、`SpriteExtractSystem` |

### 工程改动

| 文件 | 改动 |
|---|---|
| `src/engine/scene/fa_scene.vcxproj` | 新建静态库工程，GUID `{C0DE0008-…}`；引用 `fa_core`、`fa_time`、`fa_render`；`ClCompile` 用通配符 `src\*.cpp` |
| `FloriansAdventure.sln` | 新增工程节点 + 6 行配置映射 |
| `src/app/fa_app.vcxproj`、`tests/fa_tests.vcxproj` | 各新增一行 `ProjectReference` |

`fa_scene` **不引用 `fa_assets`**：`Sprite` 只存 `render::TextureHandle`（POD），纹理保活由加载方负责——asset manager 的生命周期长于任何场景。也不需要 `/EHsc`：整个 include 图里没有 `<chrono>` 与 nlohmann/json。

## 4. 核心概念与数据结构

### `EntityId` 与代际

```cpp
struct EntityId {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;
    static constexpr std::uint32_t kPendingGeneration = 0xFFFFFFFFu;

    constexpr bool isValid() const noexcept;    // index != 0 && generation != 0 && generation != kPendingGeneration
    constexpr bool isPending() const noexcept;  // generation == kPendingGeneration
};
```

- `index == 0` 是**恒不存在的槽位**（`m_entities` 的哨兵元素），所以默认构造的 `EntityId` 天然无效。
- 槽位回收时代际 +1，旧句柄的 `generation` 对不上，`isAlive()` 直接为 `false`——**悬空引用变成可检测的失败**，而不是野指针。
- 代际自增后若变成 0 或 `kPendingGeneration`，回绕到 1：这两个值被保留给「无效」和「占位」。
- `isPending()` 只有 `CommandBuffer` 的占位符会满足；真实实体永远不是 pending。

### `SparseSet<T>`

```cpp
template <typename T>
class SparseSet {
public:
    T&   add(EntityId id, T value);
    T*   tryGet(EntityId id) noexcept;
    bool has(EntityId id) const noexcept;
    bool remove(EntityId id) noexcept;
    void clear() noexcept;

    std::size_t size() const noexcept;
    std::size_t sparseCapacity() const noexcept;
    EntityId entityAt(std::size_t denseIndex) const noexcept;
    T& valueAt(std::size_t denseIndex) noexcept;
};
```

- 密集数组存 `{EntityId, T}`，稀疏数组存「密集下标 + 1」（`0` = 不存在），因此**不需要初始化稀疏数组**。
- `remove()` 用「尾部换到删除位置」实现 O(1)，代价是密集数组顺序会变——遍历顺序不保证稳定，**任何依赖顺序的逻辑都必须自己排序**。
- 按 `index` 索引意味着一个槽位同时最多一个组件实例；这是「一个实体一份 `Transform`」的直接表达。

### `World`

```cpp
class World {
public:
    EntityId create();
    bool destroy(EntityId id);
    bool isAlive(EntityId id) const noexcept;

    template <typename T> T&       add(EntityId id, T value = {});
    template <typename T> T*       tryGet(EntityId id) noexcept;
    template <typename T> const T* tryGet(EntityId id) const noexcept;
    template <typename T> bool     has(EntityId id) const noexcept;
    template <typename T> bool     remove(EntityId id) noexcept;

    template <typename T>  const SparseSet<T>& storage() const noexcept;
    template <typename... Ts, typename Fn> void each(Fn&& fn);         // fn(EntityId, Ts&...)
    template <typename... Ts, typename Fn> void each(Fn&& fn) const;   // fn(EntityId, const Ts&...)
    template <typename... Ts> std::size_t countWith() const noexcept;

    std::size_t liveEntities() const noexcept;
    std::size_t entitySlots() const noexcept;
    std::size_t registeredComponentTypes() const noexcept;
    WorldStats stats() const noexcept;
    void clear() noexcept;
};
```

- `World` **不可拷贝也不可移动**：它会成为 `BasicScene` 的成员、被系统按引用接触，允许移动只会制造悬空指针的机会。
- `add<T>()` 要求槽位存活（`FA_ASSERT`）；只读路径（`tryGet`/`has`/`each`/`countWith`/`storage`）在实体失效时安静返回空。
- **`each` 有 const 与非 const 两个重载**，因为渲染提取拿的是 `const World&`，只能用 const 版拿 `const Ts&...`。
- 只读访问一个从未出现过的组件类型**不会**创建存储：const 版 `storage<T>()` 返回静态空集，`registeredComponentTypes()` 保持不变。这有用例守着。
- `each` 以**组件数最少的那个存储**为驱动，其余类型按 `index` 探测。因此迭代是紧凑的，不会随实体槽位空洞退化。
- 实体槽位数组从 `vector<Slot>{Slot{}}` 起步，下标 0 是哨兵，所以 `entitySlots()` 是 `size() - 1`。

### `Transform` 与 `Sprite`

```cpp
struct Transform {
    core::Vec2 position{0.0f, 0.0f};
    core::Vec2 scale{1.0f, 1.0f};
    float rotation = 0.0f;              // 弧度，与 DrawItem::rotation 一致
};

struct Sprite {
    render::TextureHandle texture;
    core::Vec2 size{16.0f, 16.0f};
    core::Rect source{0.0f, 0.0f, 0.0f, 0.0f};   // 空 = 整张纹理
    core::Color tint{1.0f, 1.0f, 1.0f, 1.0f};
    render::RenderLayer layer = render::RenderLayer::Actors;
    float sortYBias = 0.0f;
    bool flipX = false;
    bool flipY = false;
    std::uint32_t entityOverride = 0;   // 0 = 用实体索引
};
```

两者都是**纯数据**（可平凡拷贝、无虚函数、无自有资源）。`Sprite::texture` 是裸句柄，句柄生命周期归加载方，这与 M06 的 `DrawItem` 完全一致。

### `CommandBuffer` 与占位符

```cpp
class CommandBuffer {
public:
    EntityId create();                                   // 返回 pending 占位符
    void destroy(EntityId target) noexcept;
    template <typename T> void add(EntityId target, T value);
    template <typename T> void remove(EntityId target);

    CommandBufferStats apply(World& world);
    EntityId resolve(EntityId id) const noexcept;        // 占位符 -> 真实句柄；未解析则原样返回
    std::size_t pendingCount() const noexcept;
    std::uint64_t appliedTotal() const noexcept;
    void clear() noexcept;
};
```

占位符的形态是 `{index = 序号递增, generation = kPendingGeneration}`，所以：

| 状态 | `isValid()` | `isPending()` | 能做什么 |
|---|---|---|---|
| 刚由 `create()` 返回 | false | true | 只能作为 `add`/`destroy` 的目标，或交给 `resolve()` |
| `apply()` 之后 | — | — | `resolve()` 给出真实句柄，拿去 `tryGet` |
| 未解析就交给 `World` | — | — | 不会命中任何实体（`index` 与 `generation` 都对不上） |

`apply()` 在缓冲为空时是幂等空操作（直接返回、不动统计）。之后内部命令表被清空，而占位符映射保留到下一次 `clear()`，所以 `apply()` 返回后仍能查映射。

目标已死的 `destroy`/`add`/`remove` 命令会**跳过而不断言**（计入 `skipped`）：场景切换期间「清理一堆可能已被别人删掉的实体」是正常操作。

## 5. 公共接口

### `ISystem` 与 `SystemPipeline`

```cpp
class ISystem {
public:
    virtual ~ISystem() = default;
    virtual std::string_view name() const noexcept = 0;
    virtual void onFixedUpdate(World& world, const time::FrameContext& context, CommandBuffer& commands) {}
    virtual void onVariableUpdate(World& world, const time::FrameContext& context, CommandBuffer& commands) {}
    virtual void onRenderExtract(const World& world, render::RenderQueue& queue) {}
};

class SystemPipeline {
public:
    void add(std::unique_ptr<ISystem> system);      // nullptr 被安静忽略
    void fixedUpdate(World& world, const time::FrameContext& context);
    void variableUpdate(World& world, const time::FrameContext& context);
    void renderExtract(const World& world, render::RenderQueue& queue);

    std::size_t size() const noexcept;
    ISystem* at(std::size_t index) noexcept;
    CommandBuffer& commands() noexcept;
    SystemPipelineStats stats() const noexcept;
    void clear() noexcept;
};
```

- 三段各自把**所有系统按注册顺序**跑一遍，每个系统之后 `drain()` → `apply()`。所以一个系统在本帧内**看不到**下一个系统的延迟创建，但下一个系统能看到自己的。
- `onRenderExtract` 拿 `const World&`：提取阶段不允许改世界，这条是编译期强制的。
- 系统**不持有 `World&` 成员**（`conventions.md` §9）：世界作为参数传入，因此同一个系统实例可以被挪到另一个世界跑，测试也不需要构造宿主。

### `IScene` 与 `BasicScene`

```cpp
class IScene {
public:
    virtual std::string_view name() const noexcept = 0;
    virtual void onEnter() {}
    virtual void onExit() {}
    virtual void onPause() {}
    virtual void onResume() {}
    virtual void onFixedUpdate(const time::FrameContext& context) {}
    virtual void onVariableUpdate(const time::FrameContext& context) {}
    virtual void onRenderExtract(render::RenderQueue& queue) {}
};

class BasicScene : public IScene {
public:
    explicit BasicScene(std::string name);       // 空名字回落为 "scene"

    World& world() noexcept;  const World& world() const noexcept;
    SystemPipeline& systems() noexcept;  const SystemPipeline& systems() const noexcept;

    void onFixedUpdate(const time::FrameContext& context) override;
    void onVariableUpdate(const time::FrameContext& context) override;
    void onRenderExtract(render::RenderQueue& queue) override;
};
```

玩法场景应当派生 `BasicScene`：它已经把 `World` 与 `SystemPipeline` 接好，重写钩子因此是**刻意的动作**，而不是必须补的样板。`world()` / `systems()` 保持公开，冒烟 app 与测试可以直接戳实体。

`BasicScene` **不重写** `onEnter`/`onExit`/`onPause`/`onResume`，派生类在这四个钩子上完全自由；只有三段更新被接到 pipeline。

### `SceneStack`

```cpp
class SceneStack {
public:
    static constexpr std::size_t kMaxDepth = 16;

    core::Status push(std::unique_ptr<IScene> scene);
    std::unique_ptr<IScene> pop();
    core::Status replace(std::unique_ptr<IScene> scene);

    IScene* top() noexcept;                       // 空栈返回 null
    IScene* at(std::size_t index) noexcept;       // 0 = 栈底
    IScene* find(std::string_view name) noexcept; // 从栈顶向下

    std::size_t size() const noexcept;
    bool empty() const noexcept;
    void clear() noexcept;

    void fixedUpdate(const time::FrameContext& context);
    void variableUpdate(const time::FrameContext& context);
    void renderExtract(render::RenderQueue& queue);

    SceneStackStats stats() const noexcept;
};
```

**核心语义：只有栈顶更新，所有场景都提取绘制。**

| 操作 | 生命周期回调 | 更新 | 提取 |
|---|---|---|---|
| `push` | **原栈顶 `onPause()`** → 新场景 `onEnter()` | 只有新栈顶 | 全部，自底向上 |
| `pop` | 栈顶 `onExit()` → **新栈顶 `onResume()`** | 只有新栈顶 | 全部，自底向上 |
| `replace` | 旧栈顶 `onExit()` → 新场景 `onEnter()`（**不** `onResume()`） | 只有新栈顶 | 全部，自底向上 |
| `clear` / 析构 | 自顶向下逐个 `onExit()` | — | — |

- 「只有栈顶更新」让暂停菜单叠在**冻结**的世界上：下层不 tick，但仍然画出来。这是暂停界面唯一正确的行为。
- `replace` 是「换关卡」而不是「关菜单」，所以不 `onResume()`——新场景是全新的世界，没有「恢复」可言。
- `push` 超过 `kMaxDepth` 返回 `kSceneStackOverflow`；`replace` 在空栈上等价于 `push`。
- `pop()` 把场景**所有权交回调用方**，调用方在 `onExit()` 之后仍能观察那个对象。`replace`/`clear`/析构则会在回调返回后立即销毁场景，那三条路径上的 `onExit()` 只能做对外部对象的副作用（存盘、释放 GPU 资源）。

### `SpriteExtractor`

```cpp
struct SpriteExtractOptions {
    bool requireTexture = true;   // 无纹理的实体是否跳过
    bool applyTransform = true;   // 是否应用缩放与旋转
    float sortYBias = 0.0f;       // 全局排序偏移（例如整体压深）
};

struct SpriteExtractStats {
    std::size_t candidates = 0;
    std::size_t submitted = 0;
    std::size_t skippedNoTexture = 0;
};

SpriteExtractStats extractSprites(const World& world, render::RenderQueue& queue,
                                  const SpriteExtractOptions& options = {});

class SpriteExtractSystem final : public ISystem {
public:
    explicit SpriteExtractSystem(SpriteExtractOptions options = {});
    std::string_view name() const noexcept override;      // "SpriteExtractSystem"
    void onRenderExtract(const World& world, render::RenderQueue& queue) override;
    const SpriteExtractStats& lastStats() const noexcept;
};
```

提取规则（每条都有用例）：

```text
sortY    = position.y + size.y + sprite.sortYBias + options.sortYBias
size     = sprite.size * transform.scale              (applyTransform = true 时)
rotation = transform.rotation                         (弧度，原样传给 DrawItem)
entityId = sprite.entityOverride != 0 ? sprite.entityOverride : id.index
texture  = sprite.texture；requireTexture 且句柄为 0 → 跳过并计入 skippedNoTexture
layer / tint / source / flipX / flipY 原样搬运
```

- **不调用 `queue.sort()`**：提取者不决定绘制顺序，排序是渲染队列的职责（M06 的排序是稳定排序，键 = 图层 → 深度键 → 实体 id）。多场景提取时全局排序必须放在所有提取之后，否则下层的绘制会排到上层前面。
- `entityId` 填实体索引而不是「第几个提取项」：它让调试器能把一次 draw call 反查回实体。
- `candidates` 是「有 `Transform`+`Sprite` 的实体数」，`submitted` 是真正入队的数量，两者之差就是被跳过的。

## 6. 使用范式

### 一帧的完整链路

```text
FrameLoop.fixedUpdate(context)
    └─ SceneStack.fixedUpdate(context)          ← 只驱动栈顶
         └─ BasicScene::onFixedUpdate
              └─ SystemPipeline.fixedUpdate
                   ├─ system[0].onFixedUpdate(world, ctx, commands)
                   ├─ commands.apply(world)      ← 结构变更在这里落地
                   ├─ system[1].onFixedUpdate(...)
                   └─ ...

FrameLoop.render(context)
    └─ renderQueue.clear()
       SceneStack.renderExtract(renderQueue)     ← 全部场景，自底向上
            └─ SpriteExtractSystem.onRenderExtract(world, renderQueue)
       renderQueue.sort()
       renderer->beginFrame(camera)
       renderer->submitAll(renderQueue.items())  ← M11 与 M06 的交界只有这一行
       renderer->endFrame()
```

`renderExtract` **不清空队列**：调用方负责 `clear()`。这样多个来源（多个场景、UI 图层）可以累积到同一个队列，最后一次性排序提交。

### 定义一个玩法系统

```cpp
class MoveMarkerSystem final : public fa::scene::ISystem {
public:
    MoveMarkerSystem(fa::scene::EntityId entity, const fa::core::Vec2& axis, float speed)
        : m_entity(entity), m_axis(&axis), m_speed(speed) {}

    std::string_view name() const noexcept override { return "MoveMarkerSystem"; }

    void onFixedUpdate(fa::scene::World& world, const fa::time::FrameContext&,
                       fa::scene::CommandBuffer& commands) override {
        static_cast<void>(commands);
        fa::scene::Transform* transform = world.tryGet<fa::scene::Transform>(m_entity);
        if (transform == nullptr) {
            return;
        }
        const float step = m_speed * static_cast<float>(fa::time::kFixedStepSeconds);
        transform->position += (*m_axis) * step;
    }

private:
    fa::scene::EntityId m_entity;
    const fa::core::Vec2* m_axis = nullptr;
    float m_speed = 0.0f;
};
```

注意这个系统**没有** `World&` 成员，也不拥有输入：它拿借用指针读外部状态，世界作为参数进来。这就是 §7 那条约定的实际形态。

### 延迟创建：先排队，再解析

```cpp
void SpawnerSystem::onFixedUpdate(World& world, const FrameContext&, CommandBuffer& commands) {
    const EntityId pending = commands.create();          // 占位符：isValid() 为 false
    commands.add<Transform>(pending, Transform{});
    commands.add<Sprite>(pending, Sprite{});
    m_pending.push_back(pending);                        // 本系统记住它，本帧别用
}

// 下一帧、下一段钩子里再解析：
for (const EntityId placeholder : m_pending) {
    const EntityId real = commands.resolve(placeholder);  // 占位符 -> 真实句柄
    if (real.isValid()) { /* 现在可以 tryGet 了 */ }
}
```

系统**不得**把占位符当真实实体用：它的 `index` 与任何槽位都对不上，`tryGet` 会安静返回 null——不会崩，但也不会工作。

### app 的接法

`src/app/main.cpp` 里 `DemoScene` 派生 `BasicScene`，在 `onEnter()` 建 920 块地面 + 1 个标记实体，注册 `MoveMarkerSystem` 与 `SpriteExtractSystem`：

```cpp
void onEnter() override {
    // 40 x 23 的地面色块，每块都是真实的实体
    for (int row = 0; row < kRows; ++row) {
        for (int column = 0; column < kColumns; ++column) {
            const EntityId id = world().create();
            world().add<Transform>(id, transform);
            world().add<Sprite>(id, sprite);
        }
    }
    m_markerEntity = world().create();
    systems().add(std::make_unique<MoveMarkerSystem>(m_markerEntity, m_axis, 120.0f));
    systems().add(std::make_unique<SpriteExtractSystem>());
}
```

`SmokeSink::renderFrame()` 只剩四行 M11 代码，取代了原先手搓瓦片与标记的三十多行：

```cpp
renderQueue.clear();
stack.renderExtract(renderQueue);
renderQueue.sort();
renderer->submitAll(renderQueue.items());
```

shutdown 时先 `stack.clear()` 再放纹理与渲染器：`onExit()` 是场景释放自己 GPU 工作的地方，不能等 GL 上下文没了才调。

## 7. 契约要点

| 契约 | 说明 |
|---|---|
| 实体句柄只能由 `World::create()` 或 `CommandBuffer::create()` 产生 | 手搓 `EntityId{1, 1}` 在测试里可以用，生产代码不行——代际是唯一防悬空的手段 |
| 销毁后旧句柄立即失效 | `isAlive()` 为 false，`tryGet` 返回 null；绝不会读到新实体的数据 |
| 结构性变更必须走 `CommandBuffer` | 在 `each()` 里直接 `create`/`destroy` 会让存储的半写状态被下一个实体看到 |
| `each` 的遍历顺序不稳定 | `remove()` 用尾部交换，密集数组顺序会变；要顺序就自己排序 |
| `each` 的 const 与非 const 是两个重载 | 提取阶段拿 `const World&`，只能用 const 版 |
| 只读访问不会创建存储 | const 版 `storage<T>()` 返回静态空集；`registeredComponentTypes()` 不变 |
| 组件是纯数据 | 无虚函数、无自有资源、可平凡拷贝；析构与生命周期归存储 |
| 系统不持有 `World&` 成员 | 世界是参数；系统因此可搬移、可单测 |
| `onRenderExtract` 不能改世界 | 签名给的是 `const World&`，编译期强制 |
| 提取者不排序 | `queue.sort()` 由调度方在所有提取之后调用一次 |
| 只有栈顶更新 | 下层暂停即冻结；这是暂停菜单的正确语义 |
| `replace` 不 `onResume()` | 换关卡不是恢复；新世界没有「之前的自己」可恢复 |
| `pop()` 交回所有权，其余路径销毁场景 | `onExit()` 在 `replace`/`clear`/析构路径上只能做对外部对象的副作用 |
| 跨实体引用只用 `EntityId` | 存指针活不过一次 `remove()` 的尾部交换 |
| `Sprite` 不保活纹理 | 只存裸句柄；asset manager 生命周期长于场景，保活不是它的职责 |
| `fa_scene` 不依赖 `fa_assets` | 纹理只以 `render::TextureHandle` 出现，接入顺序由 app 决定 |
| `World` 不可拷贝/不可移动 | 它是被引用与持有成员的对象，移动语义只会造悬空 |

## 8. 验收

环境、踩坑与复现命令见 [M11 实现记录](../build/m11-scene-ecs.md)。

| 配置（x64） | 构建 | `fa_tests` | `fa_app --frames 60` |
|---|---|---|---|
| Debug | 零 warning | 293 用例 / 17378 断言 | exit 0 |
| Development | 零 warning | 293 用例 / 17378 断言 | exit 0 |
| Release | 零 warning | 293 用例 / 17378 断言 | exit 0 |

`[scene]` 子集：**78 用例 / 286 断言**；其余模块 **215 用例 / 17092 断言**。两者相加正好等于全量，说明用例标签没有漏标。

覆盖范围：

| 标签 | 覆盖 |
|---|---|
| `[entity]` | 无效/pending 判定、`toString`、比较与 `std::hash` 可用性 |
| `[sparse]` | 增删查、紧凑性（`remove` 尾部交换）、`clear`、`entityAt`/`valueAt` |
| `[component]` | 类型键的稳定性与唯一性、`componentSetIdOf` 的区分度 |
| `[world]` | 槽位回收与代际失效、`each` 单/多组件与 const 版、`countWith`、只读不建存储 |
| `[commands]` | 占位符生命周期、`apply` 计数、跳过已死目标、`resolve` 前后行为 |
| `[pipeline]` | 三段调度顺序、每系统后刷新命令缓冲、空指针忽略 |
| `[stack]` | push/pop/replace 与暂停/恢复语义、深度上限、按名字查找、析构退出全部场景 |
| `[extract]` | sortY 公式、缩放、旋转、无纹理跳过、`entityOverride`、全局偏移、排序确定性 |
| `[integrate]` | 一整帧：建实体 → 系统运行 → 提取 → 队列内容 |

冒烟输出（三配置逐字相同）：

```text
m11: 1 scene depth, 2 system(s), 921 live entit(ies) (921 created / 0 destroyed) over 2 component type(s), 1842 component(s), 921 extracted item(s)
     stack: 1 push(es), 0 pop(s), 0 replace(s), 0 pause(s), 0 resume(s)
     920 ground tile(s), marker at (362.001, 224)
m06: 3 draw call(s), 926 quad(s), 3704 vertex/vertices, 2 texture switch(es), 0 dropped, 3 live texture(s)
M02+M03+M04+M05+M06+M07+M11 smoke test OK
```

921 实体 × 2 组件 = 1842；926 个 quad = 921 个提取项 + 5 个调试图元。
标记从 `(320, 200)` 走到 `(362.001, 224)`——120 px/s × 1 s × `(0.35, 0.2)`，说明固定步、系统调度与提取三段都对得上。

## 9. 风险与开放问题

| 风险 | 影响 | 处理 |
|---|---|---|
| 无实体原型 / prefab | 建复杂实体要在代码里逐行 `add` | v1 够用；等 M15 内容库落地后由它驱动「按定义建实体」 |
| `SparseSet::remove` 打乱顺序 | 任何依赖遍历顺序的逻辑都会偶发 | 已写进契约；需要顺序时显式排序 |
| 组件存储是 `unordered_map<ComponentTypeId, …>` | 每帧 `each` 有一次哈希查找 | 只在 `each` 开头查一次，不是每个实体；1000 实体实测无压力 |
| `detail::StorageBase` 用了虚函数 | 严格说打破了「无虚函数」的直觉 | 只用于类型擦除的 `remove`/`clear`；组件本身仍是纯数据 |
| 无 archetype / chunk 存储 | 大世界（10 万实体）缓存局部性差 | v1 是单场景 1e3 量级；真要扩容时换掉 `SparseSet` 内部即可，公共接口不变 |
| 系统顺序是隐式耦合 | 注册顺序错了会得到难查的 bug | composition root 集中注册；M27 调试面板应能列出系统名 |
| 无场景过渡（淡入淡出、异步加载） | 切场景是硬切 | `SceneStack` 已有 `replace`；过渡效果归 M25 |
| 场景与存档的关系未定 | 存世界快照还是存场景描述 | 由 M14 决定；`World` 没有 `save`/`load` 正是为了把选择留给它 |
| `kMaxDepth = 16` 是硬上限 | 深层叠加会失败 | 返回 `kSceneStackOverflow` 而不是悄悄崩；16 层远超实际需要 |
| 无空间分区 / 查询索引 | 邻近查询（碰撞、AI 感知）要线性扫 | M13/M21 需要时加空间哈希，挂在自己的模块里 |
| `Sprite::entityOverride` 是 `uint32_t` | 与 `EntityId` 的 64 位身份不一致 | 它只用于调试与稳定排序，不参与实体查找 |
