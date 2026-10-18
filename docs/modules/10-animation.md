# M10 Animation

| 项 | 值 |
|---|---|
| 层 | L1 |
| 优先级 | P1 |
| 状态 | 已实现 |
| 依赖 | M01、M06 |
| 被依赖 | M11、M16、M20、M21、M22、M26 |

> 实测与踩坑：[构建记录](../build/m10-animation.md)

## 1. 目的

提供数据驱动的精灵动画：帧序列、播放速度、循环模式、四向变体、事件帧与简单状态机。动画只影响表现，不驱动玩法判定。

## 2. 范围

**已实现**

- `AnimationClip`：`render::TextureHandle` + `AnimationFrame` 列表（源矩形、时长、可选事件名）+ 循环标记。
- `Animator`：当前 clip、时间、帧索引、速度、播放/完成标记、事件列表。
- `AnimationService`：`play` / `stop` / `update` / `currentFrame`。
- 固定步推进：累积时间跨帧、非循环结束、循环回绕、暂停（speed 0）、速度倍率。
- 动画事件：进入帧时触发；首帧事件在第一次 `update` 触发；跨多帧时每个进入的帧最多一次。
- 四向：`Direction`（Down/Up/Left/Right）、`directionFromVector` 主导轴选择。
- `AnimationStateMachine`：命名状态、方向变体、条件转换（`AnimationParams`）、优先级。
- app 集成：在 marker 之外创建一个 4 帧草地动画实体，每固定步更新 `Sprite::source`。

**未实现 / 后续**

- 混合树/上下半身分离/武器叠加；v1 只有单 clip 播放。
- 骨骼/网格变形（非目标）。
- 资源解码（M05）；clip 直接用裸 `TextureHandle`，由加载方保活。
- 帧序列编辑 UI（M26）。
- 动画事件参数化（目前是字符串 + 帧号）。
- 斜向 8 向动画；v1 用主导轴 4 向。

## 3. 核心概念与数据结构

```cpp
enum class Direction { Down, Up, Left, Right };

struct AnimationFrame {
    core::Rect source{};
    float duration = 0.1f;
    std::string event; // 空表示无事件
};

struct AnimationClip {
    render::TextureHandle texture;
    std::vector<AnimationFrame> frames;
    bool loop = true;
};

struct AnimationEvent { std::string name; std::size_t frame = 0; };

struct Animator {
    std::shared_ptr<const AnimationClip> clip;
    float time = 0.0f;
    std::size_t frameIndex = 0;
    float speed = 1.0f;
    bool playing = false;
    bool finished = false;
    bool eventPending = true;
    std::vector<AnimationEvent> events;
};
```

- `Animator` 是纯数据，可放进 ECS 组件；M10 本身不依赖 M11。
- `events` 在每次 `update` 开始时清空；调用方在 update 后消费。
- 事件在“进入该帧”时触发；首帧事件由 `eventPending` 在第一次 update 补发。
- 大 delta 跨多帧时逐个进入并触发；非循环在最后一帧停留并 `finished=true/playing=false`。

## 4. 公共接口

```cpp
namespace fa::anim {

Direction directionFromVector(core::Vec2 move, Direction fallback = Direction::Down) noexcept;

class IAnimationService {
public:
    virtual void play(Animator&, std::shared_ptr<const AnimationClip>, bool restart = false) = 0;
    virtual void stop(Animator&) = 0;
    virtual void update(Animator&, float deltaSeconds) = 0;
    virtual const AnimationFrame* currentFrame(const Animator&) const = 0;
};

class AnimationService final : public IAnimationService { /* ... */ };

struct AnimationParams {
    core::Vec2 move{};
    float speed = 0.0f;
    Direction direction = Direction::Down;
    bool running = false;
    bool attacking = false;
    bool dodging = false;
    bool hurt = false;
};

struct DirectionalClips {
    std::shared_ptr<const AnimationClip> down, up, left, right;
    std::shared_ptr<const AnimationClip> forDirection(Direction) const;
};

struct AnimationTransition {
    std::string from; // 空 = 任意状态
    std::string to;
    std::function<bool(const AnimationParams&)> condition;
    int priority = 0;
};

class AnimationStateMachine {
public:
    bool addState(std::string name, DirectionalClips clips);
    bool addState(std::string name, std::shared_ptr<const AnimationClip> clip);
    bool addTransition(AnimationTransition transition);
    bool setState(std::string_view name);
    std::string_view state() const noexcept;
    void update(Animator&, const AnimationParams&, float deltaSeconds);
    AnimationService& service() noexcept;
};

}  // namespace fa::anim
```

- 状态转换按优先级选最高；同优先级保持注册顺序。
- `setState` 强制下一次 update 重启动画。
- 同一状态内方向变化会切 clip 并重启；这是有意的简化。

## 5. 依赖关系

- 依赖 M01（`Rect`/`Vec2`/`Result`）与 M06（`render::TextureHandle`）。
- **不依赖 M05**：`AnimationClip` 只存裸 `TextureHandle`，纹理保活归加载方（与 `scene::Sprite` 一致）。
- **不依赖 M11**：`Animator` 是纯数据；`AnimationSystem` 的 ECS 调度由 composition root / M11 负责。
- 不依赖玩法模块；M16/M20 只写 `AnimationParams` 并读 `Animator::events`。
- 没有 JSON/`<chrono>`/`<unordered_map>`，因此 `fa_anim` **不需要 `/EHsc`**。

## 6. 扩展点

- 新增混合（上下半身、武器叠加）：多个 `Animator` 通道。
- 新增曲线/缓动：与精灵动画分离，放在表现层。
- 新增动画图：替换 `AnimationStateMachine`，`Animator` 接口不变。
- 事件参数化：把 `AnimationEvent` 扩展为带结构化 payload；M20 可直接消费攻击窗口。
- 新增 8 向：扩 `Direction` 与 `DirectionalClips`，状态机逻辑不变。
- 新增 clip 资源类型：M05 loader 产出 `AnimationClip`，M10 不感知文件格式。

## 7. 验收

- `fa_tests.exe "[anim]"` → 4 用例 / 59 断言全绿。
- 全量 → 462 用例 / 28885 断言全绿。
- 三配置 `fa_app.exe --frames 60` 输出 `m10: 60 update(s), frame ... , 5 event(s), ... looping`。
- Debug/Development/Release 构建零 warning。

## 8. 风险与开放问题

- 事件帧在低帧率下可能被跳过；固定步推进且跨帧逐个触发已缓解，但极端 speed 仍可能越过大量帧。
- 方向切换会重启 clip；走/跑切换可能产生轻微相位跳变。
- 没有混合；上下半身分离与武器叠加需要多通道。
- `std::shared_ptr<const AnimationClip>` 让 Animator 可安全持有 clip，但每次拷贝增加原子引用计数；动画不在热路径之外。
- 状态机条件使用 `std::function`，每帧可能分配？条件本身不分配；注册时分配一次。
- 事件是字符串，没有参数；M20 的攻击判定窗口需要结构化事件时再扩展。
- 没有动画资源格式；clip 由代码/loader 构建，真实数据驱动要等 M05/M26。
