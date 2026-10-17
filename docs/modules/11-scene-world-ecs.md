# M11 Scene / World & ECS

| 项 | 值 |
|---|---|
| 层 | L2 |
| 优先级 | P0 |
| 状态 | 设计完成 |
| 依赖 | M00–M10 |
| 被依赖 | M12–M27（几乎所有玩法模块） |

## 1. 目的

提供实体-组件-系统（ECS）世界、实体生命周期、系统调度、场景栈与渲染提取。它是玩法模块共享的唯一可变状态容器。

## 2. 范围

**负责**

- `EntityId`：32 位索引 + 32 位代际，防止悬空引用。
- 组件存储：稀疏集（sparse set），`add/get/remove/has`，组件类型注册。
- `World`：创建/销毁实体、组件访问、按视图迭代（单组件或多组件）。
- `System`：`fixedUpdate`、`variableUpdate`、`renderExtract` 三类钩子，按注册顺序执行。
- 调度：固定步系统按序执行；可变步系统在固定步之后执行；渲染提取在所有更新之后。
- 场景栈：`Scene` 接口、`push/pop/replace`、激活/停用回调、下层暂停策略。
- 实体原型（prefab）：JSON 描述的组件集合，实例化到世界。
- 渲染提取：遍历有 `Sprite`/`Transform` 的实体，生成 `DrawItem` 列表交给 M06。

**不负责**

- 具体玩法系统（M16–M25）。
- 渲染实现（M06）。
- 序列化格式（M14）与内容定义（M15）。
- 物理求解（M13）。

## 3. 核心概念与数据结构

```cpp
struct EntityId {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;
    bool isValid() const { return generation != 0; }
};

struct Transform {
    glm::vec2 position{0.0f};
    glm::vec2 scale{1.0f};
    float rotation = 0.0f;
};

struct Sprite {
    AssetHandle<Texture> texture;
    glm::vec4 sourceRect;
    glm::vec4 tint{1,1,1,1};
    RenderLayer layer = RenderLayer::Actors;
    glm::vec2 pivot{0.5f, 1.0f};
};

class World {
public:
    EntityId create();
    void destroy(EntityId id);
    template <typename T> T& add(EntityId id, T component);
    template <typename T> T* tryGet(EntityId id);
    template <typename... T> void each(Fn&& fn);
};
```

- 组件必须是可移动的纯数据；析构由存储负责。
- `each` 跳过已销毁实体；迭代期间销毁实体延迟到本系统结束。
- 系统不得跨帧保存 `EntityId` 以外的实体指针。

## 4. 公共接口草案

```cpp
namespace fa::ecs {

class ISystem {
public:
    virtual ~ISystem() = default;
    virtual void onFixedUpdate(World& world, const time::FrameContext& time) {}
    virtual void onVariableUpdate(World& world, const time::FrameContext& time) {}
    virtual void onRenderExtract(const World& world, render::IRenderer& renderer) {}
};

class IScene {
public:
    virtual ~IScene() = default;
    virtual void onEnter() = 0;
    virtual void onExit() = 0;
    virtual void onPause() {}
    virtual void onResume() {}
    virtual void onFixedUpdate(const time::FrameContext& time) = 0;
    virtual void onVariableUpdate(const time::FrameContext& time) = 0;
    virtual void onRenderExtract(render::IRenderer& renderer) = 0;
};

class ISceneStack {
public:
    virtual ~ISceneStack() = default;
    virtual void push(std::unique_ptr<IScene> scene) = 0;
    virtual void pop() = 0;
    virtual void replace(std::unique_ptr<IScene> scene) = 0;
    virtual IScene* top() = 0;
};

} // namespace fa::ecs
```

## 5. 依赖关系

- 依赖 M01、M04（时间）、M06（`DrawItem`）、M10（动画组件/提取）。
- 被所有玩法模块依赖；玩法模块只通过 `World` 访问实体，不互相持有对方系统指针。
- 系统之间的通信优先使用事件（M03）或共享的纯数据组件；避免系统直接调用其他系统。

## 6. 扩展点

- 新增组件类型：注册存储即可，无需修改 `World`。
- 新增调度阶段（`prePhysics`、`postRender`）在调度器中插入。
- 组件存储可替换为 archetype 实现而不改公共接口（v1 不做）。
- 场景过渡可扩展为异步加载 + 过渡效果。

## 7. 实现阶段

P0，Phase B。先实现实体/稀疏集/单组件迭代、`Transform`/`Sprite`、渲染提取、场景栈；再实现多组件视图、原型、延迟销毁。验收：1000 个实体稳定 60 FPS；场景切换无泄漏；销毁实体后句柄失效。

## 8. 风险与开放问题

- 系统顺序是隐式耦合；需在 composition root 明确注册顺序并文档化。
- 迭代期间结构性修改容易崩溃；统一采用延迟销毁/延迟添加命令缓冲。
- 是否允许玩法模块定义自己的组件？允许，但组件头文件不得反向 include 其他玩法模块。
- 场景与存档的关系需在 M14 明确：存档保存世界快照还是场景描述。
