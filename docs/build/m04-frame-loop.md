# M04 时间与帧循环：实现说明、实测结果与遗留风险

本文件记录 M04（Time & Frame Loop）的落地细节：真实接口形态、本机实测结果，
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
| 显示缩放 | Windows 125%（`dpiScale` 实测 1.00，见 M02 记录） |

## 实测结果

三套配置（Debug / Development / Release × x64）全部成功，**零 warning、零 error**。

```text
fa_tests.exe → All tests passed (14968 assertions in 50 test cases)    exit = 0
fa_app.exe --frames 5
               ran 5 frame(s) in 0.067s
                 fixed steps: 4 (tick 4, step 0.0167s), dropped: 0
                 simulated: 0.067s, last real delta: 0.0168s
                 average: 74.2 fps
               M02+M04 smoke test OK                                    exit = 0
```

`fa_tests` 的 50 个用例 = M01 的 22 个 + M02 新增 8 个 + M04 新增 20 个。
三配置运行结果一致。

节流长跑（`Release`，`--frames 120`，`pacingFramesPerSecond = 60`）：

```text
ran 120 frame(s) in 2.032s
  fixed steps: 121 (tick 121, step 0.0167s), dropped: 0
  simulated: 2.017s, last real delta: 0.0171s
  average: 59.0 fps
```

这是本轮最有价值的一条证据：**模拟时间（2.017 s）与墙钟（2.032 s）之差小于一个步长，
丢弃步数为 0**，说明整数 tick 权威 + 浮点余量每帧消费干净的组合在真实时钟下确实不漂移。
2.03 s 而非 2.00 s 的差额来自 `SDL_DelayNS` 的 OS 定时器粒度，属预期。

## 交付内容

### 公共头（`src/engine/include/fa/time/`）

| 文件 | 内容 |
|---|---|
| `TimeDomain.hpp` | `TimeDomain`、`kTimeDomainCount`、`toString()` |
| `FrameContext.hpp` | 5 个时间常量、`FrameContext` |
| `FixedStepClock.hpp` | 固定步累加器 |
| `TimeService.hpp` | `ITimeService`、`TimeService` |
| `FrameLoop.hpp` | `IFrameSink`、`FrameLoopConfig`、`IFrameLoop`、`FrameLoop` |

### 私有实现（`src/engine/time/src/`）

| 文件 | 内容 |
|---|---|
| `TimeDomain.cpp` | `toString()` |
| `FixedStepClock.cpp` | clamp → 累加 → 取整步 → 丢弃过剩整步 |
| `TimeService.cpp` | 各域暂停/缩放状态、帧快照填充 |
| `FrameLoop.cpp` | 事件泵 → 时钟推进 → 钩子调度 → 节流 |

### 平台层增量

`IPlatform::sleepSeconds(double)` 是本轮为 M04 新加的，理由见下文坑 #2。

## 语义决策

| 决策 | 选择 | 原因 |
|---|---|---|
| 权威时间 | 整数 tick | 浮点累加长期必漂移；`fixedStepIndex * step` 永远精确 |
| 浮点累加器职责 | 只存 `[0, step)` 余量 | 每帧消费干净，误差不会跨帧累积 |
| 死亡螺旋防护 | delta clamp 0.25 s **且** 单帧最多 5 步 | 两层上限叠加：先限输入，再限输出 |
| 超限处理 | 丢弃整步并计数 | 追不上就不追；`droppedSteps()` 是调参信号而非错误 |
| 暂停实现 | 时间服务置 0，不跳过循环 | UI 与渲染必须继续跑，否则菜单会僵死 |
| 缩放与暂停关系 | 相互独立 | 慢动作中暂停再恢复，应回到 0.5 而不是 1.0 |
| `deltaFor()` | 已乘 effectiveScale | 暂停域天然得到 0，调用方无需分支 |
| 帧内顺序 | 硬编码 | 顺序若可配置，就永远无法确定「谁先看到什么」 |
| sink 数量 | 恰好一个 | 顺序显式；多监听者由 sink 内部编排 |
| 事件派发 | 转发给 `onEvents` | 循环自己泵事件，若不转发就会被静默吞掉 |
| 睡眠位置 | `IPlatform::sleepSeconds` | 避免 `fa_time` 引入 `<thread>`/`<chrono>`，保持无异常展开 |
| 无头节拍 | `pacingFramesPerSecond` | 只服务 CI 与诊断；真实节拍归 M06 vsync |

## 踩到的坑

### 1. `FrameLoop.hpp` 若不转发事件，输入会被静默吞掉

循环内部 `pumpEvents()` 是判断退出的唯一手段，事件本身在 M07 之前没有消费方。
最初实现把 `events` 泵出来只看返回值就丢弃，导致 `fa_app` 再也收不到 resize/quit 事件，
`--window` 下的关闭按钮统计恒为 0。

**修法**：`IFrameSink` 增加 `onEvents(std::span<const platform::PlatformEvent>)`，
默认空实现，`fa_time` 只依赖 `fa/platform/PlatformEvent.hpp` 这个纯数据结构头。

### 2. 循环里 `std::this_thread::sleep_for` 会让 `fa_time` 需要 `/EHsc`

M00 定下的规则是「`/EHsc` 只给触及 `<chrono>`/`<thread>`/`<filesystem>` 的 TU」，
`fa_time` 本该是纯逻辑模块。若在 `FrameLoop.cpp` 里直接睡眠，就得给整个工程加 `/EHsc`。

**修法**：在 `IPlatform` 增加 `sleepSeconds(double)`，由已有 `<chrono>` 依赖的 `fa_platform` 实现
（`SDL_DelayNS`）。实测 `fa_time` 在 `ExceptionHandling=false` 下编译**没有** C4530，
说明 `std::vector` 等仅声明使用的用法不触发展开需求。

### 3. 第一帧 delta 为 0，`--frames 5` 只跑了 4 步

这不是 bug：`run()` 以 `startSeconds = monotonicSeconds()` 为基线，第一帧的 delta 是
「基线到事件泵返回」的间隔，节流睡眠发生在帧末，所以首帧 delta 逼近 0。
`--frames 5` 因此打印 4 个固定步。记录在此避免下次误判为丢帧。

### 4. 断言路径不能被测试触发（`std::abort` → 退出码 3）

最初写了一个「构造参数非法时回退默认值」的用例：`FixedStepClock(0.0)`。
`FA_ASSERT` 在 Debug 会走 `fa::core::fatal()`，而 `fatal()` 末尾是 `std::abort()`，
于是整个测试进程以退出码 3 直接死掉，后面的用例根本没跑。

**结论**：配置越界属于编程错误，Debug 就该炸，不做「优雅回退」的用例；
玩家输入可达的退化输入才要求优雅返回值。

### 5. 60 Hz/144 Hz 边界上的浮点截断会让「1 秒 = 60 步」少一步

`1.0/60.0` 与 `1.0/144.0` 都无法精确表示。144 帧累加后总 delta 略小于 1.0 秒，
于是 60 步变 59 步。同理，`(1.0 + 1/60) - 1.0` 会比 `1/60` 小一个 ulp，
使某一帧算出 0 步、下一帧补 2 步。

**处理**：这是固定步循环的正常现象（累加器本来就该吸收它）。测试因此改成断言
「误差不超过一个步长」而不是「恰好 60 步」；涉及逐帧步数的用例改用 `1.25 × step`
这类不落在边界的 delta，避免用例本身变成浮点彩票。

### 6. 144 Hz 用例原先只跑 1 秒，测不出漂移

只有长跑才能区分「每帧误差」与「累积漂移」。用例改成 1440 帧（10 秒），
断言模拟时间与 10.0 s 相差不超过一个步长——这正是「整数 tick 权威」要保证的性质。

## 复现命令

```powershell
$env:VCPKG_ROOT = 'D:\CPPLibraries\vcpkg'
.\scripts\build.ps1 -Configuration Development -NoInstall
.\build\bin\x64\Development\fa_tests.exe
.\build\bin\x64\Development\fa_app.exe --frames 120
```

需要真实窗口时加 `--window`（此时不节流，由 M06 vsync 接管；M06 之前会全速跑）。

## 遗留风险

| 风险 | 影响 | 处理 |
|---|---|---|
| 高刷新率下固定步与插值产生视觉抖动 | 画面轻微顿挫 | 待 M06 出画面后实机调优 |
| `sleepSeconds` 精度受 OS 定时器粒度影响 | 无头节拍略慢（2.03 s vs 2.00 s） | 仅用于诊断，不影响真实节拍 |
| 事件只转发、无消费方 | M07 之前输入事件无人处理 | `onEvents` 是明确接缝，M07 接管 |
| 无逐帧单步 | M27 调试器需要 | `setPaused` + 外部驱动已够用，需要时再加 |
| 场景栈暂停语义未定义 | 菜单弹出时下层场景如何暂停 | 由 M11 决定调用哪个域，M04 只提供能力 |
| `FrameLoop` 假定单线程 | 回调里做重活会拖慢帧 | 重活交给 M03 作业系统 |
