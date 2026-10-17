# M10 Animation

| 项 | 值 |
|---|---|
| 层 | L1 |
| 优先级 | P1 |
| 状态 | 设计完成 |
| 依赖 | M00–M06 |
| 被依赖 | M11、M16、M20、M21、M22、M26 |

## 1. 目的

提供数据驱动的精灵动画：帧序列、播放速度、循环模式、方向变体、动画事件与简单状态机。动画只影响表现，不驱动玩法判定。

## 2. 范围

**负责**

- `AnimationClip`：帧列表（纹理 + 源矩形）、帧时长、循环模式、事件帧。
- `Animator`：当前 clip、时间、播放/暂停、速度、完成回调。
- 状态机：状态 → clip 映射，按参数（速度、方向、布尔）切换，支持过渡条件。
- 方向变体：down/up/left/right，按移动向量选择。
- 动画事件：在指定帧触发命名事件（脚步、攻击判定窗口、特效生成）。
- 渲染提取：把当前帧转换为 `DrawItem`。

**不负责**

- 玩法状态机（M16/M20 的“攻击中不可移动”等规则）。
- 骨骼/网格变形（非目标，纯帧动画）。
- 资源解码（M05）。
- 帧序列编辑 UI（M26）。

## 3. 核心概念与数据结构

```cpp
struct AnimationFrame {
    glm::vec4 sourceRect;
    float duration; // 秒
    std::string event; // 可空
};

struct AnimationClip {
    AssetHandle<Texture> texture;
    std::vector<AnimationFrame> frames;
    bool loop = true;
};

struct Animator {
    const AnimationClip* clip = nullptr;
    float time = 0.0f;
    int frameIndex = 0;
    float speed = 1.0f;
    bool playing = true;
    bool finished = false;
};
```

- `Animator` 是纯数据组件，由 `AnimationSystem` 每固定步推进。
- 动画事件每帧最多触发一次；同一帧不重复触发。
- 状态机是 `Animator` 之上的薄层，可用 JSON 描述，也可代码构建。

### 闪避与无敌帧（ADR-0002 决策 1）

- 新增 `dodge` clip（4 向，各 6–8 帧，时长 0.25s）。
- 动画事件：`invulnBegin`（0.05s）与 `invulnEnd`（0.20s），由 M20 消费。
- 帧尺寸 16×24，对齐点脚底中心（ADR-0003）。
- 闪避期间不接受朝向输入，但可被硬直打断收尾（由 M16 控制过渡）。

## 4. 公共接口草案

```cpp
namespace fa::anim {

class IAnimationService {
public:
    virtual ~IAnimationService() = default;
    virtual void play(Animator& animator, const AnimationClip& clip, bool restart = false) = 0;
    virtual void stop(Animator& animator) = 0;
    virtual void update(Animator& animator, float deltaSeconds) = 0;
    virtual const AnimationFrame* currentFrame(const Animator& animator) const = 0;
};

} // namespace fa::anim
```

## 5. 依赖关系

- 依赖 M01、M05（clip 资源）、M06（`DrawItem` 类型）。
- 不依赖 M11：`AnimationSystem` 属于 M11 的 ECS 调度，但逻辑在 M10。
- M16/M20/M21 只设置 `Animator` 参数并读取事件，不直接操作帧索引。

## 6. 扩展点

- 新增混合（上下半身分离、武器叠加）作为多 `Animator` 通道。
- 新增曲线/缓动用于 UI 或相机动画（与精灵动画分离）。
- 新增“动画图”（graph）替代简单状态机；v1 不做。
- 事件可扩展为带参数的结构体而非字符串。

## 7. 实现阶段

P1，Phase B。先实现 clip 播放、方向变体、事件；再实现状态机与过渡。验收：走/跑/ idle 切换无闪烁；攻击动画在指定帧产生判定；动画结束后回到 idle。

## 8. 风险与开放问题

- 动画帧与逻辑步长不同步会产生抖动；使用累积时间并在固定步推进。
- 事件帧在低帧率下可能被跳过；固定步长可避免，但需在 `update` 中逐帧检查。
- 方向变体与斜向移动的取舍（4 向 vs 8 向）需美术规范确定。
- 动画速度受时间缩放影响还是固定？默认受 gameplay 缩放影响，UI 动画独立。
