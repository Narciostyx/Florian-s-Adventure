# M03 事件与作业：实现说明、实测结果与遗留风险

本文件记录 M03（Events & Jobs）的落地细节：真实接口形态、本机实测结果，
以及实现过程中踩到并修掉的坑。凡是「为什么这样写」的背景知识都集中在这里，
源码里只保留 Doxygen 注释。

## 实测环境

与 [M00 实测结果](m00-fixups.md) 相同：

| 项 | 值 |
|---|---|
| Visual Studio | Community 2026，18.10.12224.181 |
| MSVC 工具集 | 14.51.36231（`v145`） |
| MSBuild | `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe` |
| vcpkg | `D:\CPPLibraries\vcpkg\vcpkg.exe`（`VCPKG_ROOT` 需每次显式设置） |
| triplet | `x64-windows-static-md` |

## 实测结果

三套配置（Debug / Development / Release × x64）Rebuild 全部成功，**零 warning、零 error**。

```text
fa_tests.exe → All tests passed (16154 assertions in 76 test cases)   exit = 0
fa_tests.exe "[events]"
             → All tests passed (1186 assertions in 26 test cases)   exit = 0
fa_app.exe --frames 6
             → ran 6 frame(s) in 0.104s
               fixed steps: 5 (tick 5, step 0.0167s), dropped: 0
               m03: 5 tick event(s) (last tick 5), 5 upload event(s) worth 15
               M02+M03+M04 smoke test OK                              exit = 0
```

`fa_tests` 的 76 个用例 = M01 的 22 个 + M02 的 8 个 + M04 的 20 个 + M03 新增 26 个。
三配置运行结果一致。

`--frames 6` 只产生 5 个固定步（首帧 delta 约为 0，见 [M04 坑 #3](m04-frame-loop.md)），
所以 `m03:` 行预期是 `5 tick / 5 upload worth 15`——五次作业各提交 `1+2+3+4+5`，
合计 15，与输出一致。

> ⚠️ 这条输出在三配置下**必须逐字相同**。Debug 曾经出现 `5 tick / 4 upload`，
> 那是真实竞态，见下文坑 #5。

压力测试：每配置 8 次迭代 ×（`fa_tests` + `fa_app`），共 24 次测试 + 24 次 app 运行，**0 次失败**。
线程相关的缺陷只在低频下暴露，单次通过不算证据。

## 交付内容

### 公共头（`src/engine/include/fa/events/`）

| 文件 | 内容 |
|---|---|
| `EventBus.hpp` | `SubscriptionId`、`EventTypeId`、`EventBucket`、`EventBus`、`Subscription` |
| `JobSystem.hpp` | `JobHandle`、`JobDesc`、`JobSystem`、`kAutoWorkerCount`、`kMaxWorkerCount` |

### 私有实现（`src/engine/events/src/`）

| 文件 | 内容 |
|---|---|
| `EventBus.cpp` | 线程归属校验、退订、波次受限的队列派发、清理 |
| `JobSystem.cpp` | 工作线程池、分块并行、完成队列、析构排空 |

### 辅助改动

| 文件 | 改动 |
|---|---|
| `FloriansAdventure.sln` | 新增 `fa_events` 工程与三配置（`{C0DE0007-...}`） |
| `src/app/fa_app.vcxproj` | 引用 `fa_events` |
| `tests/fa_tests.vcxproj` | 引用 `fa_events` |
| `src/engine/include/fa/core/Log.hpp` / `Log.cpp` | 新增 `LogCategory::Events`（提交 `258249c`） |
| `src/app/main.cpp` | `EventsAndJobsDemo` 冒烟集成 |
| `tests/events.test.cpp` | 26 个用例 |

`fa_events` 只引用 `fa_core`，**不引用平台层**：事件总线与线程池在没有窗口、没有设备时可用，
这让 M01/M03 的测试完全不依赖 SDL。

## 语义决策

| 决策 | 选择 | 原因 |
|---|---|---|
| 类型键来源 | 首用即分配的 atomic 计数器，缓存进函数内 `static const` | 构建开着 `/GR-`，`typeid` 不可用；`__FUNCSIG__` 只用于诊断名 |
| 类型键可持久化 | **禁止** | 数值取决于首次使用顺序，跨构建不稳定 |
| 分桶方式 | 每种事件类型一个 `EventBucket` | 派发只遍历该类型的订阅者，不随总线规模线性增长 |
| 分发时新增订阅 | 下次分发生效 | 快照在 `dispatch` 开始时固定 |
| 分发时退订 | 立刻置 tombstone，跳过调用 | 「退订后就绝不再被调用」是 RAII 令牌能被信任的前提 |
| 内存回收时机 | 最外层 `dispatch` 返回时 `compact()` | 嵌套分发期间移动槽位会让迭代失效 |
| handler 调用方式 | 按值复制后再调用 | 防 handler 内部订阅导致 `m_slots` 重分配、引用悬空 |
| 队列派发波次上限 | 8 波，超出部分推回队列 + Warn | handler 自我喂食会死循环；宁可推迟一帧也不挂死 |
| 线程归属 | 除 `enqueue` 外全部仅拥有线程可用，违规 Fatal | 让「需不需要加锁」变成编译期就能回答的问题 |
| `enqueue` 的锁 | `std::scoped_lock` 同时锁两把 | 工作线程与主线程并发的唯一入口 |
| 作业失败传播 | 只用 `core::Status`，首个错误即停 | 全局 `/EHsc-`，作业里不允许抛异常 |
| `wait()` 的保证 | 返回时完成记录**已在**队列中 | 否则紧接的 `drainCompletedJobs()` 会扑空（坑 #5） |
| `onComplete` 执行线程 | 只在 `drainCompletedJobs()` 的调用线程 | 那里才是可以碰 GPU 的主线程 |
| `parallelFor` 等待方式 | 调用线程自己也认领分块，等待期间排空队列 | 嵌套 `parallelFor` 不会因为工作线程全被占满而死锁 |
| 析构行为 | 队列排空后才让工作线程退出 | 静默丢任务比等待更糟 |
| worker 数 | `hardware_concurrency() - 1`，clamp 到 32 | 留一个核给主线程；上限防病态机器开出几百个线程 |

## 踩到的坑

### 1. `<chrono>` 在 `/EHsc-` 下编译不过（C4530）

`JobSystem.cpp` 首次编译失败：

```text
...\include\chrono(2387): error C4530: 使用了 C++ 异常处理程序，但未启用展开语义。
```

`get_tzdb_list()` 是 `inline` 且内含真实的 `try/catch`，在 `/EHsc-` 下即便只是
`#include <chrono>` 也会被实例化，`/WX` 把 C4530 升级成错误。

**修法**（两处叠加）：

1. 从 `JobSystem.cpp` 移除 `#include <chrono>`——线程池只用 `condition_variable`/`thread`，
   本来就不需要 chrono。
2. 给 `fa_events.vcxproj` 单独加 `<ExceptionHandling>Sync</ExceptionHandling>`，
   附注释指向本文件。

**实测边界**：`<thread>`、`<mutex>`、`<condition_variable>`、`<atomic>` 在 `/EHsc-` 下
**可以用**（compaction 前用 `cl /EHsc- /W4 /WX` 探针实测产出过 `.obj`）。
只有 `<chrono>` 的 tzdb 那一段不行。所以这条限制是**局部**的，不需要给整个工程群恢复 `/EHsc`。

`Directory.Build.props` 的 `ExceptionHandling=false` 没有改动，`fa_events` 的代码本身
从不 throw、从不 catch。

### 2. `ParallelBatch` 不在 `detail` 命名空间里

写完实现后编译报「未声明的标识符」，代码里写的是 `detail::ParallelBatch`。
实际上 `ParallelBatch` 定义在 `fa::events` 的**匿名命名空间**内（为了内部链接），
并不在 `fa::events::detail` 下。

**修法**：去掉 `detail::` 限定。这类错误在头文件里看不出来，只有实现文件自己知道。

### 3. `subscribe()` 返回的是 id，不是 RAII 令牌

直觉上 `auto sub = bus.subscribe<Foo>(...)` 应该拿到一个 `Subscription`，
实际返回 `SubscriptionId`（裸整数）。设计上分离是为了让总线不必在热路径里构造对象。

**用法**：`Subscription` 的构造函数是 public 的，显式包一层即可：

```cpp
fa::events::Subscription sub(m_bus, m_bus.subscribe<Foo>(
    [this](const Foo& e) { ... }));
```

这个坑的代价是 app 集成时多编译了一轮——所以在模块文档 §6 里写明了这一行。

### 4. `wait()` 是 `[[nodiscard]]`，`/WX` 下忽略返回值报 C4834

冒烟集成里先写的是 `m_jobs.wait(handle);` 单纯等一下就完事。
`wait()` 返回 `core::Status`（无效句柄是 `kInvalidArgument`），标记了 `[[nodiscard]]`，
于是 Debug/Development 下直接编译失败。

**修法**：把 `flush()` 改成返回 `bool`，逐个检查 `wait()` 的结果，
失败时让 app 返回 1。这个改法是**对的**——等待失败意味着句柄坏了，
静默继续会让冒烟测试说谎。

### 5. Debug 下 `5 tick / 4 upload`：完成记录入队晚于置位（真 bug）

三个配置里只有 Debug 偶发少一个 upload 事件。两个缺陷叠加：

**(a) 只等最后一个句柄不够。** 早先代码只 `wait()` 最后提交的那个 handle。
句柄只保证「那个任务」完成，不保证更早提交的任务也完成了——于是早先任务的
`onComplete` 还没进队列，`drainCompletedJobs()` 就返回了。

**修法**：demo 用 `std::vector<JobHandle> m_inFlight` 记住全部句柄，`flush()` 逐个等待。

**(b) `submit` 里的顺序错了。** 原先的顺序是：

```text
state->complete.store(true);
ready.notify_all();
...之后才把完成回调 push 进 completed 队列
```

`wait()` 的谓词只看 `complete`，所以它可以在 push 之前返回，
紧接着的 `drainCompletedJobs()` 就扑空。

**修法**：把完成记录入队移到 `complete.store()` **之前**，在 `state->mutex` 内嵌套
`completedMutex` 完成 `emplace_back`；然后 `complete.store(std::memory_order_release)`；
出锁后 `notify_all()`。失败日志改为出锁后按 `logFailure` 标志输出，避免持锁打日志。

这条修正让 `wait()` 从「任务完成」升级为「任务完成**且**回调已可见」，是**全屏障**。
`JobSystem.hpp` 的 `wait()` 注释记录了这条保证，不要改回去。

### 6. 模块文档要求的「第三方异常转 `Result`」测试不可执行

`docs/modules/03-events-and-jobs.md` 的设计版曾要求「在作业边界捕获异常并转成 `core::Status`」。
当前全局 `/EHsc-`，`try/catch` 根本不能写，这条验收项**无法实现**。

**处理**：M03 的契约改为「作业不允许抛异常，失败只通过 `core::Status` 返回」，
模块文档 §7 已按此回写。真正的第三方边界捕获留给确实需要它的模块
（目前只有文件系统与资源加载，见 M02 的 `/EHsc` 适用范围）。

## 复现命令

```powershell
$env:VCPKG_ROOT = 'D:\CPPLibraries\vcpkg'
.\scripts\build.ps1 -Configuration Development -NoInstall
.\build\bin\x64\Development\fa_tests.exe
.\build\bin\x64\Development\fa_tests.exe "[events]"
.\build\bin\x64\Development\fa_app.exe --frames 6
```

三次配置逐字比对 `m03:` 行：

```powershell
foreach ($cfg in 'Debug', 'Development', 'Release') {
  .\build\bin\x64\$cfg\fa_app.exe --frames 6 |
    Select-String 'm03:'
}
```

## 全新克隆验证

M02 曾因 `.gitignore` 的 `/build/` 把 `build/*.props` 一起忽略而导致克隆后不可构建，
因此每个模块收尾都做一次克隆验证（针对已提交的 HEAD）。本轮同样通过：

```powershell
git clone --no-hardlinks . "$env:TEMP\fa-clone-m03"
cd "$env:TEMP\fa-clone-m03"
$env:VCPKG_ROOT = 'D:\CPPLibraries\vcpkg'
.\scripts\build.ps1 -Configuration Development      # 含空 vcpkg_installed 的 manifest 安装
.\build\bin\x64\Development\fa_tests.exe
.\build\bin\x64\Development\fa_app.exe --frames 60
```

```text
fa_tests.exe → All tests passed (16154 assertions in 76 test cases)   exit = 0
fa_app.exe   → ran 60 frame(s) in 1.002s
                 fixed steps: 60, dropped: 0
                 m03: 60 tick event(s) (last tick 60), 60 upload event(s) worth 1831
               M02+M03+M04 smoke test OK                              exit = 0
```

`60 tick / 60 upload` 在 60 帧长跑下仍然严格成对，说明完成队列的交接在持续负载下也稳定。

> 注：克隆到 `%TEMP%` 下会额外出现 `MSB8029`（中间目录位于临时目录），
> 这是存放位置的副作用，与本仓库配置无关；正常路径下无此警告。

## 遗留风险

| 风险 | 影响 | 处理 |
|---|---|---|
| 事件类型 id 依赖首次使用顺序 | 跨构建数值不稳定 | 契约禁止持久化；需持久化时按名称字符串匹配 |
| 波次上限 8 会推迟事件一帧 | 极端自喂食链路上出现一帧延迟 | 达到上限会 Warn；正常链路远低于 8 |
| `EventBus` 无优先级/无中断 | 无法让某个 handler 抢先 | v1 不需要；需要时在 handler 内部编排顺序 |
| 工作线程数上限 32 | 超高核数机器吃不满 | 上限是为了防止病态机器；需要时调 `kMaxWorkerCount` |
| `parallelFor` 分块固定靠 `grain` | 负载不均时尾部拖长 | 调用方给 `grain` 就能调；暂不做动态窃取 |
| 无任务优先级 | 后台预加载可能挤占前台 | M05 资源加载接入时评估 |
| 异常关闭 → 作业不能抛 | 第三方库在作业里抛会直接终止 | 需要异常边界的模块自己在作业外捕获 |
| `drainCompletedJobs()` 只在主线程调用 | 忘了调用则回调永不执行 | `fa_app` 每帧 `endFrame()` 里调用，是明确接缝 |
