# M03 Events & Jobs

| 项 | 值 |
|---|---|
| 层 | L0 |
| 优先级 | P1 |
| 状态 | 已实现 |
| 依赖 | M00、M01 |
| 被依赖 | M05、M06、M11、M14、M15、M18–M25、M27 |
| 实现记录 | [M03 事件总线与作业系统：实现说明与实测](../build/m03-events-jobs.md) |

## 1. 目的

提供两件基础设施：**类型事件总线**（模块间解耦通信）与**任务系统**（把可并行的 CPU 工作移出主线程）。两者都不包含任何玩法语义。

它们解决的问题是同一个：让上层模块彼此不认识。输入层不需要知道「谁在听跳跃」，渲染层不需要知道「谁提交了网格构建」，作业也不需要在完成时回调到具体系统。

## 2. 范围

**负责**

- 类型事件：订阅/退订、同步分发、帧末队列分发、订阅令牌生命周期。
- 事件顺序：同一类型按订阅注册顺序；跨类型按入队顺序。
- 任务系统：固定大小线程池、`JobHandle`、等待、`parallelFor`。
- 线程安全的事件入队（任意线程 → 主线程）。
- 主线程合并点：`drainCompletedJobs()`。

**不负责**

- 具体玩法事件的定义（由发布模块定义，M03 不认识任何事件类型）。
- ECS 的结构性变更（M11 的 `CommandBuffer`）。
- 资源加载策略（M05 决定何时提交任务）。
- 渲染线程/GL 上下文（M06）。
- 作业依赖图与 job stealing（v1 不做）。

## 3. 交付的文件

公共头 `src/engine/include/fa/events/`：

| 文件 | 内容 |
|---|---|
| `EventBus.hpp` | `SubscriptionId`、`EventTypeId`、`detail::EventBucket`、`EventBus`、`Subscription` |
| `JobSystem.hpp` | `JobHandle`、`JobDesc`、`JobSystem` |

私有实现 `src/engine/events/src/`：`EventBus.cpp`、`JobSystem.cpp`。
工程用 `src\*.cpp` 通配，新增实现文件无需改 `.vcxproj`。

工程 `src/engine/events/fa_events.vcxproj`（GUID `{C0DE0007-…0007}`）只引用 `fa_core`，**不引用 `fa_platform`**，因此事件与作业可以在没有窗口、没有设备的情况下测试与运行。

## 4. 核心概念与数据结构

### 事件类型 id：没有 RTTI 的类型键

工程全局 `<RuntimeTypeInfo>false</RuntimeTypeInfo>`（`/GR-`），`typeid` 不可用，因此类型键由「首用即分配」的原子计数器产生：

```
std::atomic<EventTypeId>{0}  →  ++  →  每个类型一次，函数内 static const 缓存
```

同一个类型在任意翻译单元、任意线程里都拿到同一个 id。类型名通过 `__FUNCSIG__` 截取并从 `struct `/`class `/`enum ` 前缀剥离，仅用于诊断输出，不参与相等比较。

代价是：id 只在**首次使用时**分配，所以具体数值取决于首次使用的顺序。不要把 id 写进存档、配置或网络协议。

### 分桶与槽位

每种事件类型一个 `EventBucket<E>`，桶内是 `Slot{ id, active, handler }` 的数组。分桶让一次分发只触碰该类型的订阅者，而不是全部订阅者。

退订不立即从数组移除，而是把槽位标记为 `active = false`（墓碑）；最外层分发返回时才 `std::erase_if` 压缩。这样分发过程中退订不会让正在迭代的数组失效。

### 快照语义

`dispatch()` 开始时固定 `snapshot = m_slots.size()`，只遍历 `[0, snapshot)`：

| 分发过程中的动作 | 结果 |
|---|---|
| 新增订阅 | 本次分发不调用，下一次生效 |
| 退订自己或后续订阅者 | 立刻置墓碑，同一次分发中后续不再调用 |
| 同类型嵌套 `publish` | 独立快照，内层先完整跑完 |

handler 在调用前按值复制一份：handler 内部的订阅/退订可能让 `m_slots` 重分配，直接引用会悬空。

### 波次上限

`dispatchQueued()` 先 `swap` 出队列，最多跑 `kMaxDispatchWaves = 8` 轮；每轮把 handler 新入队的事件追加到队尾继续处理。超过 8 轮仍未稳定，剩余事件推回队列并记一条 Warn，下一帧重试。

这个上限的意义是把「handler 反复 enqueue 自己」的死循环从「卡死主线程」降级为「日志里刷警告」。

### 线程归属

| 操作 | 允许的线程 |
|---|---|
| `subscribe` / `unsubscribe` / `publish` / `dispatchQueued` / `clear` | 仅拥有线程（构造 `EventBus` 的线程） |
| `enqueue` | 任意线程 |

违规调用打一条 Fatal 日志再 `core::fatal`。这不是可恢复错误：让非拥有线程直接分发事件会让槽位数组的并发假设失效，继续跑下去只会得到更难查的崩溃。

`enqueue` 是作业 worker 回主线程的唯一通道，所以它必须线程安全：`std::scoped_lock` 保护队列。

### 作业状态机

`JobState` 由 `shared_ptr` 持有，`JobHandle` 只是它的句柄（可复制、可失效）：

```
submit → queued → running → { status, complete=true, ready.notify_all() }
```

- `complete` 用 `release` 存、`acquire` 读，配 `condition_variable` 的谓词等待。
- **完成回调在 `complete` 置位之前入队**，因此 `wait()` 返回后 `drainCompletedJobs()` 一定能跑到对应回调。这条顺序是刻意设计的，反过来（先通知再入队）会产生「等待返回了但这一帧 drain 不到」的偶发空洞。

### parallelFor 的分块与防死锁

`parallelForErased(count, grain, fn)` 把 `[0, count)` 按 `grain` 切成块，用 `std::atomic<std::size_t>` 让 worker 认领下一块（不需要锁，也不需要预先分配任务对象）。

关键点是**调用线程自己也干活**：压入 `helperCount = min(workerCount, chunkCount)` 个 helper 之后，调用线程原地跑 `runChunks()`；等待期间不阻塞，而是 `tryRunOneTask()` 从队列里取别的任务，取不到才 `yield`。

因此 worker 内部再调用 `parallelFor`（嵌套使用）不会死锁——即使所有 worker 都在等自己的子任务，队列里的分块仍会被某个等着的线程执行。

首错即停：任一块返回错误就置 `failed`，其余块认领时立刻放弃，`firstError` 在 `errorMutex` 保护下只记录第一次失败。

## 5. 公共接口

### 事件总线

```cpp
using SubscriptionId = std::uint64_t;
inline constexpr SubscriptionId kInvalidSubscriptionId = 0;
inline constexpr int kMaxDispatchWaves = 8;

class EventBus final {
public:
    EventBus() = default;                       // 拥有线程 = 构造线程
    EventBus(const EventBus&) = delete;

    template <typename E>
    SubscriptionId subscribe(std::function<void(const E&)> handler);

    void unsubscribe(SubscriptionId id) noexcept;

    template <typename E>
    std::size_t publish(const E& event);        // 返回实际调用的 handler 数

    template <typename E>
    void enqueue(E event);                      // 任意线程

    std::size_t dispatchQueued();
    void clear();
    std::size_t queuedCount() const noexcept;

    template <typename E>
    std::size_t subscriberCount() const noexcept;
    std::size_t totalSubscriberCount() const noexcept;
};
```

### 订阅令牌

```cpp
class Subscription final {
public:
    Subscription() = default;
    Subscription(EventBus& bus, SubscriptionId id) noexcept;

    Subscription(Subscription&& other) noexcept;             // 移动，不复制
    Subscription& operator=(Subscription&& other) noexcept;

    ~Subscription();                                          // 自动退订

    bool isValid() const noexcept;
    SubscriptionId id() const noexcept;
    void reset() noexcept;                                    // 立即退订，可重复调用
    SubscriptionId release() noexcept;                        // 放弃所有权，保留订阅
};
```

令牌不拥有总线，必须比总线先销毁——实践中就是**声明在总线之后**。

### 作业

```cpp
inline constexpr std::uint32_t kAutoWorkerCount = 0;   // 取 hardware_concurrency() - 1
inline constexpr std::uint32_t kMaxWorkerCount = 32;

struct JobDesc {
    std::string name;                     // 仅用于失败日志
    std::function<core::Status()> run;
};

class JobHandle final {
public:
    JobHandle() = default;
    bool isValid() const noexcept;
    bool isComplete() const noexcept;
};

class JobSystem final {
public:
    explicit JobSystem(std::uint32_t workerCount = kAutoWorkerCount);
    ~JobSystem();                                    // 排空队列后 join

    JobHandle submit(JobDesc desc);
    core::Status wait(JobHandle handle);             // 无效句柄返回 kInvalidArgument

    template <typename Fn>
    core::Status parallelFor(std::size_t count, std::size_t grain, Fn&& fn);
    core::Status parallelForErased(std::size_t count, std::size_t grain,
                                   const std::function<core::Status(std::size_t)>& fn);

    void onComplete(JobHandle handle, std::function<void(core::Status)> callback);
    std::size_t drainCompletedJobs();

    std::uint32_t workerCount() const noexcept;
    std::size_t queuedJobCount() const noexcept;
};
```

`parallelFor` 的 body 返回 `void` 视为「无失败」，返回 `core::Status` 则传播错误。

## 6. 使用范式

```cpp
// 订阅：令牌声明在总线之后，离开作用域自动退订。
fa::events::EventBus bus;
fa::events::Subscription sub(bus, bus.subscribe<TickCompleted>([](const TickCompleted& event) {
    world.advance(event.tick);
}));

// 同步分发：调用返回时所有 handler 都跑完了。
const std::size_t delivered = bus.publish(TickCompleted{tick});

// 作业 + 回主线程：body 在 worker 跑，回调在 drain 线程跑。
fa::events::JobSystem jobs{2};
const fa::events::JobHandle handle = jobs.submit(fa::events::JobDesc{
    "mesh-upload", []() { return fa::core::okStatus(); }});
jobs.onComplete(handle, [&bus](fa::core::Status status) {
    if (status.isOk()) {
        bus.enqueue(UploadFinished{1});   // worker 的结论安全地交给主线程
    }
});

// 每帧末尾两行：先合并作业完成，再分发入队事件。
jobs.drainCompletedJobs();
bus.dispatchQueued();
```

`src/app/main.cpp` 的 `EventsAndJobsDemo` 就是这套组合的最小可运行例子，冒烟测试每帧各走一次。

## 7. 契约要点

| 约定 | 说明 |
|---|---|
| 分发是同步的 | `publish` 返回时 handler 已全部执行完毕，不存在「稍后调用」 |
| 快照语义 | 分发中新增订阅下一次才生效；退订立刻生效 |
| 波次上限 8 | 防止 handler 自我喂食造成死循环；超出的事件推回队列并记 Warn |
| 线程归属 | 除 `enqueue` 外全部要求拥有线程，违规 Fatal 而非静默 |
| `enqueue` 任意线程 | worker 回主线程的唯一通道 |
| `wait()` 是全屏障 | 返回时完成回调已入队，紧随其后的 drain 一定跑得到 |
| 回调只在 drain 线程执行 | GPU 上传、缓冲创建这类必须在主线程做的事放这里，不放 job body |
| 作业不抛异常 | 全局 `/EHsc-`，失败一律走 `core::Status` |
| 首错即停 | `parallelFor` 任一块失败后其余块放弃，返回第一个错误 |
| 析构先排空 | `JobSystem` 析构让 worker 跑完已接受的队列再退出，不静默丢任务 |
| 不依赖平台层 | 只引用 `fa_core`；线程数由 composition root 传入 |
| 类型 id 不是稳定值 | 首次使用顺序决定数值，禁止持久化 |

## 8. 验收

- [x] 订阅按注册顺序调用，多类型互不串扰。
- [x] 退订幂等；未知 id 与重复退订都是 no-op。
- [x] `Subscription` 析构自动退订，`release()` 后保留订阅。
- [x] 分发中新增订阅下一次才生效；分发中退订跳过其后的 handler。
- [x] 同类型嵌套 `publish` 顺序确定为内层先完成。
- [x] `enqueue` 延迟到 `dispatchQueued`，跨类型按入队顺序。
- [x] 4 线程 × 100 次并发 `enqueue` 无丢失、无崩溃。
- [x] 类型 id 同类型跨调用稳定，类型名可从 `__FUNCSIG__` 还原。
- [x] 作业成功返回 `ok`，失败把 code 7002 原样传到 `wait`。
- [x] 无效句柄 `wait` 返回 `kInvalidArgument` 而不阻塞。
- [x] 单 worker、多 worker 下 `parallelFor` 都覆盖每个下标且只执行一次。
- [x] `parallelFor` 首错返回（code 7003），`count == 0` 直接成功，`grain == 0` 按 1 处理。
- [x] `onComplete` 回调只在 `drainCompletedJobs()` 线程执行。
- [x] worker 数解析 ≥ 1，显式值被夹到 32。
- [x] 析构时队列里 32 个任务全部执行完。
- [x] 三配置（Debug / Development / Release）零警告零错误，26 用例 / 1186 断言全绿。
- [x] `fa_app` 冒烟运行固定 5 步 → 5 个 tick 事件、5 个上传事件（值合计 15），三配置一致。

## 9. 风险与开放问题

| 风险 | 影响 | 处理 |
|---|---|---|
| 订阅者销毁时忘记退订 | 悬空回调 | 用 `Subscription` 成员而非裸 id，析构自动退订 |
| 类型 id 数值依赖使用顺序 | 存档/网络不可用 id | 已在契约中禁止持久化；需要稳定键时由上层定义显式枚举 |
| 非拥有线程误调 `publish` | 进程终止 | 刻意 Fatal：并发分发会产生更难查的数据竞争 |
| 事件尚无真实消费方 | 目前只有冒烟演示 | M07 输入、M06 渲染接手后才会出现真实生产者 |
| 无 job stealing | 长任务尾部分块可能拖慢一帧 | v1 分块均匀足够；出现实测瓶颈再加 |
| 作业里做阻塞 IO | 线程池被占满，主线程 `parallelFor` 也变慢 | 约定作业只做 CPU 工作；加载交给 M05 的异步通道 |
| 本工程单独开 `/EHsc` | 与全局「异常关闭」不完全一致 | 只影响 `fa_events`，代码本身从不 throw；细节见实现记录 |
| 回调顺序未定义 | 多个作业同一帧完成时次序不稳定 | 需要顺序时在一次 `parallelFor` 内表达，或自行排序 |
