# M04 Time & Frame Loop

| 项 | 值 |
|---|---|
| 层 | L0 |
| 优先级 | P0 |
| 状态 | 设计完成 |
| 依赖 | M00、M01、M02 |
| 被依赖 | M05、M06、M11、M22、M25、M27 |

## 1. 目的

提供确定性的固定步长主循环、帧阶段调度、暂停/时间缩放与帧统计。玩法只通过 `FrameContext` 感知时间，不直接读取真实时钟。

## 2. 范围

**负责**

- `FrameClock`：真实帧 delta、FPS、帧序号。
- `FixedClock`：固定 1/60 秒步长、累加器、最大补步数。
- `TimeDomain`：Gameplay / UI / Cinematic 的暂停与缩放。
- `FrameContext`：传给所有系统的只读时间视图。
- 循环驱动：`beginFrame → fixedUpdate × N → variableUpdate → render → endFrame`。
- 帧统计：CPU 时间、逻辑步数、渲染提交数。

**不负责**

- 游戏内日历/季节（M22）。
- 具体系统的更新顺序（由 composition root 注册）。
- 渲染帧率控制/垂直同步（M02/M06）。

## 3. 核心概念与数据结构

```cpp
struct FrameContext {
    std::uint64_t frameIndex;
    double frameDeltaSeconds;
    double fixedDeltaSeconds;
    float interpolationAlpha;
    float gameplayScale;
    bool isPaused;
};

enum class TimeDomain { Gameplay, UI, Cinematic };

class FrameClock {
public:
    void beginFrame();
    void endFrame();
    double frameDeltaSeconds() const;
    double elapsedSeconds() const;
    std::uint64_t frameIndex() const;
};
```

- `interpolationAlpha` 用于渲染插值；逻辑系统不使用它做判定。
- `gameplayScale = 0` 表示暂停；`UI` 域默认不受影响。
- 补步上限 5；超过时丢弃剩余累加器并记录警告。

## 4. 公共接口草案

```cpp
namespace fa::time {

class ITimeService {
public:
    virtual ~ITimeService() = default;
    virtual void setPaused(TimeDomain domain, bool paused) = 0;
    virtual void setScale(TimeDomain domain, float scale) = 0;
    virtual float scale(TimeDomain domain) const = 0;
    virtual const FrameContext& context() const = 0;
};

class IFrameLoop {
public:
    virtual ~IFrameLoop() = default;
    virtual int run() = 0;
    virtual void requestQuit() = 0;
};

} // namespace fa::time
```

## 5. 依赖关系

- 依赖 M01（日志/断言）与 M02（时钟、事件泵）。
- 不依赖 M03；主循环自己管理阶段顺序。
- 上层通过 `ITimeService` 读取时间；不直接持有 `FrameClock`。

## 6. 扩展点

- 可替换补步策略（固定上限、动态上限、掉帧降级）。
- 可增加 `TimeDomain`（如 `Physics`、`Network`）。
- 可增加确定性回放：记录每帧输入与固定步数。
- 可接入性能分析器，输出每阶段耗时。

## 7. 实现阶段

P0，Phase A。实现 `FrameClock`、固定步长累加器、`FrameContext`、暂停/缩放。测试：60/30/144 FPS 下逻辑步数正确；长帧不产生死亡螺旋；暂停时玩法不推进、UI 仍更新。

## 8. 风险与开放问题

- 高刷新率显示器下固定步长与渲染插值的视觉抖动需要调优。
- 浮点累加器长期运行可能漂移；使用整数 tick 计数作为权威时间。
- 暂停语义需与场景栈一致：下层场景暂停，UI/菜单继续。
- 是否需要“逐帧单步”调试？M27 可实现，但需要 M04 暴露接口。
