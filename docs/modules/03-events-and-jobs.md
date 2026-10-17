# M03 Events & Jobs

| 项 | 值 |
|---|---|
| 层 | L0 |
| 优先级 | P1 |
| 状态 | 设计完成 |
| 依赖 | M00、M01 |
| 被依赖 | M05、M06、M11、M14、M15、M18–M25、M27 |

## 1. 目的

提供两件基础设施：**类型事件总线**（模块间解耦通信）与**任务系统**（把可并行的 CPU 工作移出主线程）。两者都不包含任何玩法语义。

## 2. 范围

**负责**

- 类型事件：订阅/退订、立即分发、帧末队列分发、订阅令牌生命周期。
- 事件顺序：同一类型按订阅注册顺序；跨类型按入队顺序。
- 任务系统：固定大小线程池、`JobHandle`、等待/轮询、parallel-for。
- 线程安全队列与轻量 future 封装。
- 主线程合并点：`drainCompletedJobs()`。

**不负责**

- 具体玩法事件的定义（由发布模块定义）。
- ECS 的结构性变更（M11 的 `CommandBuffer`）。
- 资源加载策略（M05 决定何时提交任务）。
- 渲染线程/GL 上下文（M06）。

## 3. 核心概念与数据结构

```cpp
using SubscriptionId = std::uint64_t;

class EventBus {
public:
    template <typename E>
    SubscriptionId subscribe(std::function<void(const E&)> handler);

    void unsubscribe(SubscriptionId id);

    template <typename E>
    void enqueue(const E& event);

    void dispatchQueued();
};

class JobSystem {
public:
    explicit JobSystem(std::uint32_t workerCount);
    JobHandle submit(JobDesc desc);
    void wait(JobHandle handle);
    void parallelFor(std::size_t count, std::size_t grain, std::function<void(std::size_t)> fn);
};
```

- 事件队列按类型分桶，避免每次分发遍历所有类型。
- `SubscriptionId` 在退订后失效；重复退订是安全的 no-op。
- `JobHandle` 可复制；等待只允许主线程或明确允许的线程。

## 4. 公共接口草案

```cpp
namespace fa::events {

class IEventBus {
public:
    virtual ~IEventBus() = default;
    virtual void dispatchQueued() = 0;
    virtual void clear() = 0;
};

class IJobSystem {
public:
    virtual ~IJobSystem() = default;
    virtual JobHandle submit(JobDesc desc) = 0;
    virtual void wait(JobHandle handle) = 0;
    virtual std::uint32_t workerCount() const = 0;
};

} // namespace fa::events
```

## 5. 依赖关系

- 只依赖 M01（`Result`、日志、断言）与标准库线程设施。
- 不依赖 M02，避免事件/任务被平台层绑定；线程数可由 composition root 传入。
- 上层模块可以依赖 M03；M03 不 include 任何上层头。

## 6. 扩展点

- 事件优先级：在 `enqueue` 增加优先级参数；v1 不需要。
- 任务依赖图：`JobHandle` 增加 `dependsOn`；v1 先做简单 parallel-for。
- 线程命名与亲和性：Windows 下 `SetThreadDescription`，便于调试。
- 事件调试：Debug 下记录事件类型计数与订阅者数量，供 M27 展示。

## 7. 实现阶段

P1，Phase B/C 前完成。先实现事件总线（含退订与帧末分发）、固定线程池、`parallelFor`。测试：订阅/退订、重入、跨线程入队、任务异常路径（第三方异常转 `Result`）。

## 8. 风险与开放问题

- 订阅者对象销毁时忘记退订会导致悬空回调；约定订阅令牌由订阅者持有，并在析构中退订。
- 事件在分发过程中新增订阅/退订的行为需定义；v1 采用“本帧快照”语义，避免迭代器失效。
- 任务系统过度使用会引入同步开销；只有大于阈值的批量工作才提交。
- 是否需要 job 窃取？v1 不需要，固定队列足够。
