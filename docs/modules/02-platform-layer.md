# M02 Platform Layer

| 项 | 值 |
|---|---|
| 层 | L0 |
| 优先级 | P0 |
| 状态 | 设计完成 |
| 依赖 | M00、M01、SDL3 |
| 被依赖 | M03、M04、M05、M07、M08、M09、M26、M27 |

## 1. 目的

把 SDL3、Windows 文件系统、DPI、消息框等平台能力封装在稳定接口后面，使引擎其余部分不直接依赖 SDL。它是唯一允许 include `<SDL3/SDL.h>` 的模块。

## 2. 范围

**负责**

- 初始化/关闭 SDL3 子系统（视频、音频、计时器、游戏手柄）。
- 窗口创建、销毁、标题、尺寸、全屏、DPI、像素缩放。
- 事件泵：把 SDL 事件转换为引擎 `PlatformEvent`。
- 文件系统：读/写/存在/创建目录/枚举；资源路径解析。
- 剪贴板、消息框、打开文件对话框（编辑器用）。
- 高精度时钟（供 M04 使用）。
- 平台错误转换为 `fa::core::Error`。

**不负责**

- 输入映射与动作状态（M07）。
- OpenGL 上下文创建细节（M06 与 M02 协作，但 GL 调用归 M06）。
- 资源缓存与解码（M05）。
- 线程池（M03）。

## 3. 核心概念与数据结构

```cpp
struct WindowDesc {
    std::string title;
    int width;
    int height;
    bool resizable;
    bool highDpi;
    bool fullscreen;
};

enum class PlatformEventType {
    Quit, Resized, FocusGained, FocusLost,
    KeyDown, KeyUp, MouseDown, MouseUp, MouseMove, MouseWheel,
    GamepadAxis, GamepadButton, TextInput, FileDrop
};

struct PlatformEvent {
    PlatformEventType type;
    // union/struct payload，按类型解释
};
```

- 事件是值类型，不带 SDL 指针；M07 消费后转换为动作状态。
- 文件路径统一使用 UTF-8 `std::string`；Windows 宽字符转换在实现内完成。

## 4. 公共接口草案

```cpp
namespace fa::platform {

class IWindow {
public:
    virtual ~IWindow() = default;
    virtual void setTitle(std::string_view title) = 0;
    virtual void resize(int width, int height) = 0;
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual float dpiScale() const = 0;
    virtual void* nativeHandle() const = 0; // 仅供 M06 创建 GL 上下文
};

class IFileSystem {
public:
    virtual ~IFileSystem() = default;
    virtual bool exists(std::string_view path) const = 0;
    virtual Result<std::string> readText(std::string_view path) = 0;
    virtual Result<std::vector<std::byte>> readBinary(std::string_view path) = 0;
    virtual Result<void> writeText(std::string_view path, std::string_view content) = 0;
    virtual Result<void> createDirectories(std::string_view path) = 0;
};

class IPlatform {
public:
    virtual ~IPlatform() = default;
    virtual Result<void> initialize() = 0;
    virtual void shutdown() = 0;
    virtual bool pumpEvents(std::vector<PlatformEvent>& outEvents) = 0;
    virtual IWindow& window() = 0;
    virtual IFileSystem& files() = 0;
    virtual double monotonicSeconds() const = 0;
};

} // namespace fa::platform
```

## 5. 依赖关系

- 依赖 M01 的 `Result`/`Error`；依赖 SDL3。
- M02 不依赖 M03/M04；M04 通过 `IPlatform` 获取时钟与事件。
- M06 通过 `nativeHandle()` 创建 GL 上下文；这是受控的指针传递，不把 SDL 类型暴露给其他模块。

## 6. 扩展点

- 新增平台后端：实现 `IPlatform`/`IWindow`/`IFileSystem`，在 composition root 选择。
- 新增窗口标志（无边框、透明、HDR）在 `WindowDesc` 增加字段，默认关闭。
- 新增文件系统后端（内存、pak）实现 `IFileSystem`，M05 不感知。
- 新增事件类型：扩展 `PlatformEventType`，同步更新 M07。

## 7. 实现阶段

P0，Phase A。先实现：SDL 初始化、窗口、事件泵、文件读写、时钟。验收：创建 1280×720 窗口、响应关闭、能读写 UTF-8 文本、事件队列无丢失。

## 8. 风险与开放问题

- SDL3 API 仍在演进；锁定 vcpkg 版本并避免直接使用实验性 API。
- 高 DPI 下像素风缩放可能出现非整数倍；M06 需要虚拟分辨率与整数缩放配合。
- 文件写入需要原子替换（临时文件 + rename），M02 提供基础能力，M14 负责存档语义。
- 是否允许 M02 直接创建 GL 上下文？当前设计是 M06 调用 `nativeHandle()` 创建；需在 M00/M06 验证 SDL3 + WGL 流程。
