# M04 Time & Frame Loop

| 项 | 值 |
|---|---|
| 层 | L0 |
| 优先级 | P0 |
| 状态 | 已实现 |
| 依赖 | M00、M01、M02 |
| 被依赖 | M05、M06、M07、M11、M22、M25、M27 |
| 实现记录 | [M04 时间与帧循环：实现说明与实测](../build/m04-frame-loop.md) |

## 1. 目的

提供确定性的固定步长主循环、帧阶段调度、暂停/时间缩放与帧统计。
玩法只通过 `FrameContext` 感知时间，不直接读取真实时钟，因此同一套逻辑在 30、60、144 Hz 下表现一致。

## 2. 范围

**负责**

- `FixedStepClock`：把不规则的墙钟 delta 转成整数个固定步，带防死亡螺旋上限。
- `TimeDomain`：Gameplay / Ui / Cinematic 三个域，各自的暂停与缩放互不影响。
- `FrameContext`：传给所有系统的只读时间快照。
- 循环驱动：`pumpEvents → onEvents → 推进时钟 → onFixedUpdate × N → onVariableUpdate → onRender`。
- 帧统计：帧序号、固定步数、丢弃步数、真实/缩放 delta、插值系数。
- 无头节拍（`pacingFramesPerSecond`），供 CI 与诊断使用。

**不负责**

- 游戏内日历/季节/时段（M22）。
- 输入映射与动作状态（M07）。循环只把 `PlatformEvent` 原样转发给 sink。
- 垂直同步与真实呈现节拍（M06）。
- 具体系统的更新顺序（由 composition root 在 `IFrameSink` 内部编排）。
- 线程与作业调度（M03）。

## 3. 交付的文件

公共头 `src/engine/include/fa/time/`：

| 文件 | 内容 |
|---|---|
| `TimeDomain.hpp` | `TimeDomain`、`kTimeDomainCount`、`toString()` |
| `FrameContext.hpp` | 全部时间常量、`FrameContext` |
| `FixedStepClock.hpp` | 固定步累加器 |
| `TimeService.hpp` | `ITimeService`、`TimeService` |
| `FrameLoop.hpp` | `IFrameSink`、`FrameLoopConfig`、`IFrameLoop`、`FrameLoop` |

私有实现 `src/engine/time/src/`：`TimeDomain.cpp`、`FixedStepClock.cpp`、`TimeService.cpp`、`FrameLoop.cpp`。
工程显式列出源文件与公共头（VS2026 不再使用通配符）；新增实现文件时要同时改 `.vcxproj`。

## 4. 核心概念与数据结构

### 时间常量

集中定义在 `FrameContext.hpp`，改一处即全局生效：

| 常量 | 值 | 含义 |
|---|---|---|
| `kFixedStepsPerSecond` | 60 | 逻辑步频率 |
| `kFixedStepSeconds` | 1/60 | 单步时长 |
| `kMaxCatchUpSteps` | 5 | 单帧最多补步数 |
| `kMaxRealDeltaSeconds` | 0.25 | 单帧接受的墙钟 delta 上限 |
| `kMaxTimeScale` | 8.0 | 时间缩放上限 |

1/60 秒 = 16.67 ms，与 M01 `TimeUnits`、M22 日历刻度一致。
0.25 秒上限意味着一次卡顿最多补 15 步，而补步上限 5 又会丢弃其余整步，两次上限叠加保证不会出现「越卡越慢」的死亡螺旋。

### 帧快照

```cpp
struct FrameContext {
    std::uint64_t frameIndex = 0;
    std::uint64_t fixedStepIndex = 0;      // 权威 tick 计数
    double realDeltaSeconds = 0.0;         // 墙钟，已 clamp
    double scaledDeltaSeconds = 0.0;       // 受暂停/缩放影响
    double fixedDeltaSeconds = kFixedStepSeconds;
    float interpolationAlpha = 0.0f;       // [0,1)，仅供渲染
    float gameplayScale = 1.0f;            // 暂停时为 0
    std::uint32_t fixedStepsThisFrame = 0;
    bool isPaused = false;
};
```

字段分成两组，用途不同：

- **真实时间**（`realDeltaSeconds`、`frameIndex`）：UI 动画、加载进度、日志节拍。暂停时仍前进。
- **缩放时间**（`scaledDeltaSeconds`、`gameplayScale`）：玩法逻辑。暂停时为 0。

`interpolationAlpha` 只允许渲染使用。逻辑代码若按它分支，渲染与模拟就会对「刚刚发生了什么」产生分歧。

### 整数 tick 权威

模拟时间 = `fixedStepIndex * fixedStepSeconds`。
浮点累加器只保存 `[0, step)` 的余量，每帧消费干净，因此长期运行不会累积漂移。
测试用 144 Hz 跑 10 秒验证：误差始终在一个步长以内。

## 5. 公共接口

### 固定步累加器

```cpp
class FixedStepClock {
public:
    explicit FixedStepClock(double fixedStepSeconds = kFixedStepSeconds,
                            int maxStepsPerFrame = kMaxCatchUpSteps,
                            double maxRealDeltaSeconds = kMaxRealDeltaSeconds) noexcept;

    void reset() noexcept;
    int advance(double realDeltaSeconds) noexcept;

    double fixedStepSeconds() const noexcept;
    int maxStepsPerFrame() const noexcept;
    double maxRealDeltaSeconds() const noexcept;
    double lastDeltaSeconds() const noexcept;
    double pendingSeconds() const noexcept;
    float interpolationAlpha() const noexcept;
    std::uint64_t fixedStepIndex() const noexcept;
    double simulatedSeconds() const noexcept;
    std::uint64_t droppedSteps() const noexcept;
};
```

纯逻辑：无时钟、无平台、无分配，因此可以脱离真实时间做确定性测试。
构造参数越界时在 Debug 触发断言（属于配置错误，不是玩家输入可达的路径），Release 回退默认值。

### 时间服务

```cpp
class ITimeService {
public:
    virtual void setPaused(TimeDomain domain, bool paused) = 0;
    virtual bool isPaused(TimeDomain domain) const = 0;
    virtual void setScale(TimeDomain domain, float scale) = 0;
    virtual float scale(TimeDomain domain) const = 0;
    virtual float effectiveScale(TimeDomain domain) const = 0;
    virtual double deltaFor(TimeDomain domain) const = 0;
    virtual const FrameContext& context() const = 0;
};
```

`deltaFor(domain)` 返回 `realDeltaSeconds * effectiveScale(domain)`，暂停域恒为 0，调用方不必额外加分支。
暂停与缩放彼此独立：`setScale(Gameplay, 0.5f)` 之后暂停再恢复，缩放仍是 0.5，而不是被重置成 1.0。
`setScale` 会把值夹到 `[0, kMaxTimeScale]`，非有限值直接忽略。

### 帧循环

```cpp
struct FrameLoopConfig {
    double fixedStepSeconds = kFixedStepSeconds;
    int maxFixedStepsPerFrame = kMaxCatchUpSteps;
    double maxRealDeltaSeconds = kMaxRealDeltaSeconds;
    int pacingFramesPerSecond = 0;   // 0 = 不节流
    std::uint64_t maxFrames = 0;      // 0 = 跑到用户关闭
};

class IFrameSink {
public:
    virtual void onEvents(std::span<const platform::PlatformEvent> events) {}
    virtual void onFixedUpdate(const FrameContext& context) {}
    virtual void onVariableUpdate(const FrameContext& context) {}
    virtual void onRender(const FrameContext& context) {}
};

class FrameLoop final : public IFrameLoop {
public:
    FrameLoop(platform::IPlatform& platform, IFrameSink& sink, FrameLoopConfig config = {});

    int run() override;
    void requestQuit() override;
    const ITimeService& time() const override;

    TimeService& timeService();
    const FixedStepClock& clock() const;
    std::uint64_t frameCount() const;
};
```

## 6. 使用范式

```cpp
class GameSink final : public fa::time::IFrameSink {
public:
    void onFixedUpdate(const fa::time::FrameContext& context) override { world.update(context.fixedDeltaSeconds); }
    void onRender(const fa::time::FrameContext& context) override { renderer.draw(context.interpolationAlpha); }
};

GameSink sink;
fa::time::FrameLoop loop(*platform, sink);
fa::time::FrameLoopConfig config;
config.maxFrames = 3;                                       // CI 冒烟
loop.timeService().setPaused(fa::time::TimeDomain::Gameplay, true);  // 暂停菜单
const int exitCode = loop.run();
```

暂停通过时间服务表达，不是跳过循环：`onVariableUpdate` 与 `onRender` 继续执行，UI 因此仍然有响应。

## 7. 契约要点

| 约定 | 说明 |
|---|---|
| 帧内顺序固定 | `pumpEvents → onEvents → 推进时钟 → onFixedUpdate × N → onVariableUpdate → onRender`，不可配置 |
| 单一 sink | 顺序显式；需要固定位置放系统时，在 sink 内部编排，而不是让循环支持多监听者 |
| 只有 Gameplay 驱动补步 | 固定步只跑 Gameplay 域；UI 与渲染逐帧推进 |
| `pumpEvents` 返回 false 时仍派发 | 最后一帧的事件（含 Quit）先交给 sink 再退出，否则关闭事件会被吞掉 |
| 时钟不可用即失败 | `monotonicSeconds()` 为 0 时 `run()` 返回非零：没有时钟就没有可测量的帧时长 |
| 丢弃步数不是错误 | `droppedSteps()` 是非零只说明机器跟不上，是调参信号 |
| 不在循环里睡眠做节流 | 只给无头运行用 `pacingFramesPerSecond`；真实节拍交给 M06 的 vsync |
| 不引入 `<thread>` | 睡眠走 `IPlatform::sleepSeconds`，`fa_time` 因此保持无异常展开，不必加 `/EHsc` |

## 8. 验收

- [x] 60 Hz 帧产生 1 步，30 Hz 帧产生 2 步，144 Hz 长跑无漂移。
- [x] 长帧被 clamp 到 0.25 s 且不超过 5 步，多余整步计入 `droppedSteps()`。
- [x] 零、负数、NaN、无穷 delta 全部被忽略。
- [x] `interpolationAlpha` 始终落在 `[0, 1)`。
- [x] 暂停后玩法 delta 为 0，UI 仍按真实时间推进。
- [x] 缩放与暂停彼此独立，非法缩放被夹取或忽略。
- [x] 钩子顺序为 `F V R` 且每帧恰好一次 variable/render。
- [x] 真实窗口下 120 帧耗时 2.03 s（≈59 fps），121 步模拟 2.017 s，丢弃 0 步。
- [x] 三配置（Debug / Development / Release）零警告零错误，50 用例 / 14968 断言全绿。

## 9. 风险与开放问题

| 风险 | 影响 | 处理 |
|---|---|---|
| 高刷新率下固定步与插值产生视觉抖动 | 画面轻微顿挫 | 需要实机调优；`interpolationAlpha` 已按渲染需求暴露 |
| 首次 `pumpEvents` 之后 `monotonicSeconds` 仍可能为 0（极早调用） | 循环立即返回失败 | 已在文档与实现中显式失败，而不是静默用 0 delta 空转 |
| `sleepSeconds` 精度受 OS 定时器粒度影响 | 无头节拍略有抖动（实测 2.03 s / 期望 2.00 s） | 仅用于诊断；真实节拍交给 vsync |
| 逐帧单步调试尚未提供 | M27 调试器需要步进能力 | `TimeService` 的暂停 + 循环外部驱动已足够，需要时再加 |
| 事件只是转发，尚无消费方 | 输入事件在 M07 之前无人处理 | `onEvents` 是明确的临时接缝，M07 接管后 sink 内部委托给输入层 |
| 场景栈暂停语义未定义 | 菜单弹出时下层场景该如何暂停 | 由场景栈（M11）决定调用哪个域的 `setPaused`，M04 只提供能力 |
