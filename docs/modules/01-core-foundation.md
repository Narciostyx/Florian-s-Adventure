# M01 Core Foundation

| 项 | 值 |
|---|---|
| 层 | L0 |
| 优先级 | P0 |
| 状态 | 已实现 |
| 依赖 | M00 |
| 被依赖 | 全部模块 |

## 1. 目的

提供不依赖平台、窗口、图形的最小基础库：错误处理、日志、ID、随机数、数学别名、字符串/哈希工具、断言与性能计时。它是所有模块的“共同语言”，必须稳定、可单元测试。

## 2. 范围

**负责**

- `Result<T>` / `Error` / 错误码。
- 日志门面：分类、级别、线程安全、文件轮转。
- 断言与 fatal handler。
- `EntityId` 之外的通用强类型 ID 模板、哈希、字符串工具。
- 随机数 `Rng`（显式种子、可复制状态）。
- 数学别名（`Vec2`、`Rect`、`Color`）与常用插值/几何函数。
- 时间单位类型（秒/毫秒/tick）与计时器工具。
- 性能计时宏与帧统计的轻量容器。

**不负责**

- 窗口、文件、线程池、事件总线（M02/M03）。
- ECS 的 `EntityId` 与组件存储（M11）。
- 内容 ID 的语义（M15）。
- 渲染数学的矩阵/相机（M06）。

## 3. 核心概念与数据结构

```cpp
struct Error {
    int code = error_code::kUnknown;
    std::string message;
};

template <typename T>
class [[nodiscard]] Result {
public:
    static Result ok(T value);
    static Result err(int code, std::string message);
    bool isOk() const noexcept;
    bool isError() const noexcept;
    const T& value() const&;
    T take() &&;
    const Error& error() const&;
    T valueOr(T fallback) const;
};

class Rng {  // PCG32
public:
    void seed(std::uint64_t seed) noexcept;
    void seed(std::uint64_t seed, std::uint64_t stream) noexcept;
    std::uint64_t state() const noexcept;
    std::uint64_t increment() const noexcept;
    void restore(std::uint64_t state, std::uint64_t increment) noexcept;
    std::uint32_t nextU32() noexcept;
    std::uint32_t bounded(std::uint32_t bound) noexcept;
    int range(int minInclusive, int maxExclusive) noexcept;
    float nextFloat01() noexcept;
    float nextFloatRange(float minInclusive, float maxExclusive) noexcept;
    bool chance(float probability) noexcept;
    std::size_t nextIndex(std::size_t count) noexcept;
};
```

- `Result<T>` 不使用异常；`value()` / `error()` 在状态不匹配时触发 `FA_ASSERT`。
- `Rng` 为 PCG32；`state()` / `increment()` / `restore()` 使随机流可存档复现，`bounded()` 用拒绝采样保证无偏。
- 退化边界为**合法输入**而非断言失败：`bounded(0)` 返回 0，`range(min, min)` 与反向区间返回 `min`。这样 Debug 与 Release 行为完全一致——玩法代码里“候选列表为空”是常态，不应在 Debug 构建中崩溃。
- 日志使用 `std::string_view` 与固定栈缓冲 + `vsnprintf`，被级别过滤时完全跳过格式化。

### 实际落地文件

```text
src/engine/include/fa/core/
  Assert.hpp  Error.hpp  Hash.hpp  Ids.hpp  Log.hpp
  Math.hpp   Result.hpp  Rng.hpp   TimeUnits.hpp  Version.hpp
src/engine/core/src/
  Log.cpp  Assert.cpp  Rng.cpp  Hash.cpp  Version.cpp
tests/core.test.cpp   # 22 个用例 / 14626 条断言
```

## 4. 公共接口草案

```cpp
namespace fa::core {

// Error.hpp —— 错误码按模块分段，Core 占 1000–1999
namespace error_code {
inline constexpr int kUnknown = 1000;
inline constexpr int kInvalidArgument = 1001;
inline constexpr int kOutOfRange = 1002;
inline constexpr int kNotFound = 1003;
inline constexpr int kAlreadyExists = 1004;
inline constexpr int kUnsupported = 1005;
inline constexpr int kIoFailure = 1006;
inline constexpr int kParseFailure = 1007;
inline constexpr int kAssertionFailed = 1008;
}

// Log.hpp
enum class LogLevel : std::uint8_t { Trace, Debug, Info, Warn, Error, Fatal, Off };
enum class LogCategory : std::uint8_t {
    Core, Platform, Time, Asset, Render, Input, Audio, Ui, Animation, Ecs,
    Tilemap, Physics, Save, Content, Gameplay, Ai, Economy, Editor, Count,
};

class ILogSink {
public:
    virtual ~ILogSink() = default;
    virtual void write(LogLevel level, LogCategory category, std::string_view message) = 0;
    virtual void flush() {}
};

void addLogSink(ILogSink* sink);
void removeLogSink(ILogSink* sink);
void flushLogs();
void setLogLevel(LogLevel minimumLevel) noexcept;
bool isLogEnabled(LogLevel level) noexcept;
void log(LogCategory category, LogLevel level, std::string_view message);
void logf(LogCategory category, LogLevel level, const char* format, ...);

// Assert.hpp
using FatalHandler = void (*)(std::string_view message);
void setFatalHandler(FatalHandler handler) noexcept;
bool reportCheckFailure(std::string_view expression, std::string_view file,
                        int line, std::string_view message);

// Math.hpp（Rect 为独立结构体，Vec2/Color 复用 glm）
using Vec2 = glm::vec2;
using Color = glm::vec4;
struct Rect { float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f; };

constexpr Rect makeRect(float x, float y, float w, float h) noexcept;
constexpr Rect rectFromCenter(Vec2 center, float w, float h) noexcept;
constexpr bool rectOverlaps(const Rect& a, const Rect& b) noexcept;   // 半开区间
constexpr bool rectContains(const Rect& r, Vec2 point) noexcept;
constexpr bool rectContainsRect(const Rect& outer, const Rect& inner) noexcept;
constexpr Rect intersect(const Rect& a, const Rect& b) noexcept;
constexpr Rect inset(const Rect& r, float dx, float dy) noexcept;
constexpr float lerp(float a, float b, float t) noexcept;
constexpr Color lerp(Color a, Color b, float t) noexcept;
float snapToPixel(float value) noexcept;

// Ids.hpp
template <typename Tag, typename T = std::uint32_t>
class StrongId { /* value(), isValid(), std::hash 特化 */ };

// Hash.hpp
constexpr std::uint64_t fnv1a64(std::string_view text) noexcept;
constexpr std::uint64_t hashCombine(std::uint64_t seed, std::uint64_t value) noexcept;
std::uint64_t hashBytes(const void* data, std::size_t size) noexcept;

// TimeUnits.hpp
struct Seconds { double value = 0.0; };
struct Milliseconds { double value = 0.0; };
class Countdown { /* start / stop / isRunning / remaining / progress / tick */ };

// Result.hpp 的零载荷通道
struct Unit {};
using Status = Result<Unit>;
Status okStatus();
Status errorStatus(int code, std::string message);

} // namespace fa::core
```

错误传递与 `Rect` 形态的取舍见 [ADR-0004](../adr/0004-core-math-and-result-shape.md)。

## 5. 依赖关系

- 只依赖 C++ 标准库与 glm；不 include SDL3/OpenGL。
- 被所有模块依赖；因此接口变更必须谨慎，优先新增而不是修改。
- 不反向依赖任何模块。

## 6. 扩展点

- 新增日志 sink（控制台、文件、ImGui 控制台）通过 `ILogSink` 注册。
- 新增错误码按模块分段（如 1000–1999 Core，2000–2999 Platform）。
- 新增数学类型放在 `fa/core/Math.h`，避免散落。
- 性能计时可替换为 Tracy 等工具，但宏接口保持稳定。

## 7. 实现阶段

P0，已完成（与 M00 同步落地）。

已交付：`Error`/`Result`/`Status`、`Log`（19 分类 + sink 注册 + 惰性默认 stderr sink）、`Assert`（`FA_ASSERT`/`FA_VERIFY`/`FA_CHECK` + fatal handler）、`Rng`（PCG32）、`Math`（`Vec2`/`Rect`/`Color` + 几何与插值）、`Ids`（`StrongId`）、`Hash`（FNV-1a 64）、`TimeUnits`（`Seconds`/`Milliseconds`/`Countdown`）、`Version`。

测试基线：`tests/core.test.cpp` 共 22 个用例 / 14626 条断言，Debug、Development、Release 三配置全部通过。后续修改不得降低该基线。

## 8. 风险与开放问题

已解决

- `Rect` 形态：采用独立结构体（见 [ADR-0004](../adr/0004-core-math-and-result-shape.md)）。
- 日志格式化：使用 1024 字节栈缓冲 + `vsnprintf`，不依赖 `std::format`。
- 错误传递：C++20 下使用自研 `Result<T>`，不启用异常。

仍存在

- 关闭异常后，标准库 OOM 会 terminate；依赖 `setFatalHandler` 给出可诊断信息，但不保证能优雅恢复。
- 日志 sink 固定 8 个槽位且为非拥有指针，调用方必须在 sink 生命周期内先 `removeLogSink`。
- `Result` 的错误路径不带调用栈，定位依赖日志与断言消息。
- `snapToPixel` 在极端大世界坐标下会因浮点精度失准，v1 世界尺寸远小于该阈值，暂不处理。
