# M02 Platform Layer

| 项 | 值 |
|---|---|
| 层 | L0 |
| 优先级 | P0 |
| 状态 | 已实现 |
| 依赖 | M00、M01、SDL3 |
| 被依赖 | M03、M04、M05、M07、M08、M09、M26、M27 |
| 实现记录 | [M02 平台层：实现说明与实测](../build/m02-platform.md) |

## 1. 目的

把 SDL3、Windows 文件系统、DPI、时钟、消息框等平台能力封装在稳定接口后面，使引擎其余部分不直接依赖 SDL。
它是**唯一**允许 `#include <SDL3/SDL.h>` 的模块。

## 2. 范围

**负责**

- 初始化/关闭 SDL3 子系统（视频、事件、游戏手柄）。
- 窗口创建、标题、尺寸、全屏、DPI、原生句柄。
- 事件泵：把 SDL 事件转换为引擎 `PlatformEvent`。
- 文件系统：存在性、目录判断、文本/二进制读写、原子写、建目录、枚举、删除。
- 高精度单调时钟（供 M04 使用）。
- 模态消息框（启动失败等场景）。
- 平台错误转换为 `fa::core::Error`（错误码段 2000–2999）。

**不负责**

- 输入映射与动作状态（M07）。
- OpenGL 上下文创建与 GL 调用（M06）。M02 只交出 `nativeHandle()`。
- 资源缓存与解码（M05）。
- 线程池（M03）。
- 音频设备打开（M08；当前只初始化 SDL 子系统，不建流）。
- 存档语义（M14 用 `writeTextAtomic` 实现多槽与自动存档）。

## 3. 交付的文件

公共头 `src/engine/include/fa/platform/`：

| 文件 | 内容 |
|---|---|
| `PlatformError.hpp` | 命名空间 `platform_error`，8 个错误码 |
| `PlatformEvent.hpp` | `PlatformEventType`、`MouseButton`、`PlatformEvent`、`toString()` |
| `Window.hpp` | `WindowDesc`、`IWindow` |
| `FileSystem.hpp` | `IFileSystem` |
| `Platform.hpp` | `MessageBoxKind`、`IPlatform`、`createPlatform()` |

私有实现 `src/engine/platform/src/`：`SdlPlatform`、`SdlWindow`、`StdFileSystem`、`PlatformEvent.cpp`。
工程用 `src\*.cpp` 通配，新增实现文件无需改 `.vcxproj`。

## 4. 核心概念与数据结构

### 窗口描述

```cpp
struct WindowDesc {
    std::string title = "Florian's Adventure";
    int width = 640;          // 逻辑尺寸
    int height = 360;         // 逻辑尺寸
    bool resizable = true;
    bool highDpi = true;
    bool fullscreen = false;
    bool hidden = false;
};
```

`width/height` 是**逻辑尺寸**（等于设计稿的虚拟分辨率 640×360），与显示器缩放无关。

### 事件

```cpp
enum class PlatformEventType : std::uint8_t {
    Quit, WindowResized, FocusGained, FocusLost,
    KeyDown, KeyUp, MouseDown, MouseUp, MouseMove, MouseWheel,
    TextInput, FileDrop
};

struct PlatformEvent {
    PlatformEventType type = PlatformEventType::Quit;
    std::int32_t scancode = 0;
    std::int32_t keycode = 0;
    bool repeat = false;
    MouseButton button = MouseButton::None;
    float x = 0.0f, y = 0.0f, dx = 0.0f, dy = 0.0f;
    std::int32_t width = 0, height = 0;
    std::string text;          // UTF-8
};
```

- 事件是**纯值类型**，不含 SDL 指针，消费者可自由复制保存。
- 不做联合体优化：单帧事件量级是几十个，平坦结构体更简单也更好调试。
- `toString(PlatformEventType)` 供日志与调试面板使用。

### 文件系统

路径一律 UTF-8 `std::string`；Windows 宽字符转换在实现内部完成，不依赖进程 ANSI 代码页。

## 5. 公共接口

```cpp
namespace fa::platform {

class IWindow {
public:
    virtual ~IWindow() = default;
    virtual void setTitle(std::string_view title) = 0;
    virtual void resize(int width, int height) = 0;
    virtual void setFullscreen(bool enabled) = 0;
    virtual int width() const = 0;          // 可绘制像素尺寸
    virtual int height() const = 0;         // 可绘制像素尺寸
    virtual float dpiScale() const = 0;     // 像素尺寸 / 逻辑尺寸
    virtual bool isFullscreen() const = 0;
    virtual void* nativeHandle() const = 0; // HWND，仅供 M06
};

class IFileSystem {
public:
    virtual ~IFileSystem() = default;
    virtual bool exists(std::string_view path) const = 0;
    virtual bool isDirectory(std::string_view path) const = 0;
    virtual fa::core::Result<std::string> readText(std::string_view path) = 0;
    virtual fa::core::Result<std::string> readBinary(std::string_view path) = 0;
    virtual fa::core::Status writeText(std::string_view path, std::string_view content) = 0;
    virtual fa::core::Status writeTextAtomic(std::string_view path, std::string_view content) = 0;
    virtual fa::core::Status createDirectories(std::string_view path) = 0;
    virtual fa::core::Result<std::vector<std::string>> listDirectory(std::string_view path) = 0;
    virtual fa::core::Status remove(std::string_view path) = 0;
};

class IPlatform {
public:
    virtual ~IPlatform() = default;
    virtual fa::core::Status initialize(const WindowDesc& desc) = 0;
    virtual void shutdown() = 0;
    virtual bool pumpEvents(std::vector<PlatformEvent>& outEvents) = 0;
    virtual bool quitRequested() const = 0;
    virtual IWindow& window() = 0;
    virtual IFileSystem& files() = 0;
    virtual double monotonicSeconds() const = 0;
    virtual fa::core::Status showMessageBox(MessageBoxKind kind, std::string_view title,
                                            std::string_view message) = 0;
};

std::unique_ptr<IPlatform> createPlatform();

}  // namespace fa::platform
```

### 使用范式

```cpp
auto platform = fa::platform::createPlatform();
if (auto status = platform->initialize({}); !status) { /* 记录并退出 */ }

std::vector<fa::platform::PlatformEvent> events;
while (running) {
    events.clear();
    if (!platform->pumpEvents(events)) break;   // false = 用户请求退出
    for (const auto& event : events) { /* 交给 M07 / M04 */ }
}
platform->shutdown();
```

### 契约要点

| 契约 | 说明 |
|---|---|
| `pumpEvents` 返回 `bool` | false 表示收到 Quit；调用方无需再查 `quitRequested()` |
| `pumpEvents` **不清空**传入 vector | 调用方自己 `clear()`，便于跨帧累积或在两处消费 |
| `initialize` 幂等 | 重复调用返回 `okStatus()`，不重建窗口 |
| `width()/height()` 不可缓存 | 隐藏窗口与非整数 DPI 下会变化，渲染层每帧查询 |
| `Status` 而非 `Result<void>` | 仓库不提供 `Result<void>`，统一用 `fa::core::Status` |
| `monotonicSeconds()` 未初始化返回 0.0 | 用 0.0 表示“时钟不可用”，不抛错不断言 |
| `writeTextAtomic` 保证读方看到完整文件 | 临时文件 + rename；Windows 上需先删目标 |
| `showMessageBox` 失败降级 stderr | 无显示环境下仍能看到错误文本 |

## 6. 依赖关系

- 依赖 M01 的 `Result`/`Status`/`Unit`/`Error`；依赖 SDL3（唯一允许 include 的模块）。
- M02 不依赖 M03/M04/M06；M04 通过 `IPlatform` 获取时钟与事件。
- M06 通过 `nativeHandle()` 拿到 `HWND` 创建 WGL 上下文——这是引擎里唯一合法的
  “把平台句柄交给别的层”的通道，句柄所有权始终留在窗口侧。
- 只产出**可执行文件**的工程需要导入 `build/Sdl3.props`（SDL3 静态库的 Windows 系统依赖）。

## 7. 扩展点

- **换平台后端**：实现 `IPlatform`/`IWindow`/`IFileSystem`，在 composition root 里换掉 `createPlatform()`。
- **新窗口标志**（无边框、透明、HDR）：`WindowDesc` 加字段，默认关闭。
- **新文件系统后端**（内存、pak、云存档）：实现 `IFileSystem`，M05/M14 不感知。
- **新事件类型**：扩展 `PlatformEventType`，同步更新 `translateEvent`、`toString` 与 M07。
- **手柄**：v1 不需要，但 SDL 侧已初始化 GAMEPAD 子系统，M07 接上时只需扩展映射表。

## 8. 验收

- [x] 创建 1280×720 窗口并响应关闭
- [x] 事件队列无丢失（`Quit`/`WindowResized`/键鼠/文本/拖放全覆盖映射）
- [x] 中文路径与中文内容读写往返正确
- [x] 二进制往返 256 字节全等
- [x] 原子写不残留 `.tmp`
- [x] `createDirectories` 幂等、`listDirectory` 结果有序
- [x] 单调时钟可用且单调递增
- [x] `initialize` 幂等
- [x] 三配置（Debug / Development / Release）Rebuild 零警告零错误
- [x] `fa_tests` 30 个用例全绿（14681 断言）

## 9. 风险与开放问题

| 问题 | 结论/状态 |
|---|---|
| M02 是否直接创建 GL 上下文？ | **已定**：不创建。M02 创建窗口时带上 `SDL_WINDOW_OPENGL` 标志并提供 `nativeHandle()`，WGL 上下文由 M06 建 |
| 高 DPI 下像素风非整数缩放 | 未解决，归 M06：需虚拟分辨率 + 整数倍缩放 + 黑边策略 |
| `SDL_Quit` 是全局状态 | 未解决：同一进程只能有一个 `IPlatform` 存活；将来做多窗口编辑器需引用计数 |
| 无显示设备的 CI | 窗口用例降级为 `WARN` 跳过，覆盖率下降；后续可在 CI 上跑 Xvfb 等价方案或拆出无窗口用例 |
| 手柄抽象 | 接口预留（SDL 子系统已初始化），但 `PlatformEvent` 暂无手柄事件；v1 不启用 |
| 消息框阻塞事件泵 | 只在启动失败等场景使用，不当断言工具 |
| 剪贴板/文件对话框 | 编辑器（M26）需要，暂未实现；`IPlatform` 留了扩展位 |

更多实现细节与踩坑见 [M02 平台层：实现说明与实测](../build/m02-platform.md)。
