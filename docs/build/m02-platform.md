# M02 平台层：实现说明、实测结果与遗留风险

本文件记录 M02（Platform Layer）的落地细节：真实的 SDL3 接口形态、本机实测结果，
以及实现过程中踩到并修掉的坑。凡是“为什么这样写”的背景知识都集中在这里，
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
| SDL3 | vcpkg 端口，产出 `SDL3-static.lib` |
| 显示缩放 | Windows 125%（用于验证 DPI 语义） |

## 实测结果

三套配置（Debug / Development / Release × x64）`/t:Rebuild` 全部成功，**零 warning、零 error**。

```text
fa_app.exe   → window 1280x720 px, dpi scale 1.00
               ran 3 frame(s) in 0.256s (quit events: 0, resize events: 0)
               M02 platform smoke test OK                              exit = 0
fa_tests.exe → All tests passed (14681 assertions in 30 test cases)     exit = 0
```

`fa_tests` 的 30 个用例 = M01 的 22 个 + M02 新增 8 个。Debug 与 Release 的运行结果与
Development 一致。

## 交付内容

### 公共头（`src/engine/include/fa/platform/`）

| 文件 | 内容 |
|---|---|
| `PlatformEvent.hpp` | `PlatformEventType`、`MouseButton`、`PlatformEvent`、`toString()` |
| `Window.hpp` | `WindowDesc`、`IWindow` |
| `FileSystem.hpp` | `IFileSystem` |
| `Platform.hpp` | `MessageBoxKind`、`IPlatform`、`createPlatform()` |
| `PlatformError.hpp` | 2000–2007 平台错误码 |

### 私有实现（`src/engine/platform/src/`）

| 文件 | 内容 |
|---|---|
| `SdlWindow.hpp/.cpp` | SDL3 窗口，含 DPI、原生句柄 |
| `SdlPlatform.hpp/.cpp` | SDL 初始化、事件泵、时钟、消息框、`createPlatform()` |
| `StdFileSystem.hpp/.cpp` | `std::filesystem` + 二进制 iostream 的文件系统 |
| `PlatformEvent.cpp` | `toString()` 实现 |

`fa_platform.vcxproj` 用 `src\*.cpp` 通配，新增实现文件不需要改工程。

### 测试（`tests/platform.test.cpp`）

8 个 `TEST_CASE`：文本往返、缺失文件错误码、UTF-8 路径与内容、二进制往返、
原子写无残留、`createDirectories` 幂等与目录排序、事件默认值与 `toString`、
隐藏窗口 + 单调时钟 + `initialize` 幂等。无显示设备的 CI 环境里窗口用例降级为 `WARN` 跳过。

## 语义决策（已固化，改动需同步文档）

| 决策 | 理由 |
|---|---|
| `IPlatform::initialize()` 返回 `Status`（即 `Result<Unit>`） | 仓库里没有 `Result<void>`；`Unit` 让所有调用点写法统一 |
| `pumpEvents(vector&)` 返回 `bool`，false 表示用户已请求退出 | 循环写成 `while (platform->pumpEvents(events))`，不需要额外查 `quitRequested()` |
| `WindowDesc.width/height` 是**逻辑尺寸** | 与设计稿的“虚拟分辨率”一致，和显示器缩放解耦 |
| `IWindow::width()/height()` 是**可绘制像素尺寸** | 渲染后端（M06）要的正是帧缓冲尺寸 |
| `IWindow::dpiScale()` = 可绘制像素 / 逻辑尺寸 | 见下面的坑 4，必须用 `SDL_GetWindowPixelDensity` |
| 事件是纯值类型，UTF-8 用 `std::string` 承载 | 不泄漏 SDL 指针，消费者可自由复制保存 |
| 路径一律 UTF-8 `std::string` | Windows 宽字符转换在实现内部完成，不依赖进程 ANSI 代码页 |
| `writeTextAtomic()` 走临时文件 + rename | 存档（M14）要求读方永远看到完整文件 |
| 未初始化时 `monotonicSeconds()` 返回 0.0 | 便于调用方判断“时钟不可用”，不抛错也不断言 |
| `showMessageBox()` 失败时降级到 stderr | 无显示环境下仍能看到错误文本，同时返回 `kMessageBoxFailed` |

`nativeHandle()` 返回 Win32 `HWND`，M06 用它创建 WGL 上下文；这是引擎里唯一合法的
“把平台句柄交给别的层”的通道，句柄所有权始终留在窗口。

## 已知坑（均已修复，勿回退）

### 1. `<chrono>` / `<thread>` / `<filesystem>` 需要 `/EHsc`

`Directory.Build.props` 全局关掉异常（`ExceptionHandling=false`），但 MSVC 的
`<chrono>`、`<thread>`、`<filesystem>` 实现内部使用 `try/throw`。一旦某个翻译单元
引入它们，就会得到致命错误（`/WX` 下）：

```text
std\include\chrono(2387,9): error C2220: 以下警告被视为错误
std\include\chrono(2387,9): warning C4530: 使用了 C++ 异常处理程序，但未启用展开语义。请指定 /EHsc
```

受影响的工程是 `fa_platform`（用 `<filesystem>`）与 `fa_app`（用 `<chrono>`/`<thread>`），
两者各自加了：

```xml
<ItemDefinitionGroup>
  <ClCompile>
    <ExceptionHandling>Sync</ExceptionHandling>
  </ClCompile>
</ItemDefinitionGroup>
```

**这不等于“引擎开始用异常”**：这只打开栈展开表，引擎的 API 仍然只用 `Result`/`Status`
表达错误。纯逻辑模块（`fa_core`、`fa_time`、后续玩法模块）不要跟着加，保持关掉更有约束力。

### 2. vcpkg 只注入 `SDL3-static.lib`，不注入它的系统依赖库

`x64-windows-static-md` 的 SDL3 是静态库，它自己依赖一批 Windows 系统库。vcpkg 的 MSBuild
集成只把 `SDL3-static.lib` 加进链接，系统库需要自己补，否则连接阶段一片：

```text
SDL3-static.lib(SDL_timer.c.obj) : error LNK2019: 无法解析的外部符号 __imp_timeBeginPeriod
SDL3-static.lib(SDL_windowsevents.c.obj) : error LNK2019: 无法解析的外部符号 __imp_SetupDiGetDeviceInstanceIdA
SDL3-static.lib(SDL_windowskeyboard.c.obj) : error LNK2019: 无法解析的外部符号 ImmGetContext
SDL3-static.lib(SDL_windows_gaming_input.c.obj) : error LNK2019: 无法解析的外部符号 __imp_CM_Get_Parent
fatal error LNK1120: 24 个无法解析的外部命令
```

权威来源是 vcpkg 安装树里的 `share\sdl3\SDL3staticTargets.cmake`（`INTERFACE_LINK_LIBRARIES`）。
已固化为 `build/Sdl3.props`：

```text
kernel32 user32 gdi32 winmm imm32 ole32 oleaut32 version uuid
advapi32 setupapi shell32 cfgmgr32 dinput8
```

只有**产出可执行文件/动态库**的工程需要导入它（当前是 `fa_app` 与 `fa_tests`）；
`fa_platform` 是静态库，没有链接阶段，不要在里面写。

> 这一条修正了 [M00 实测结果](m00-fixups.md) 第 4 条“不需要也不应该有任何链接配置”的旧结论：
> 库名确实不要手写，但静态库的系统依赖必须补。

### 3. `SDL_CreateWindow` 的参数顺序与 SDL2 不同

SDL3 把 title 放在了第一位：

```c
SDL_Window *SDL_CreateWindow(const char *title, int w, int h, SDL_WindowFlags flags);
```

沿用 SDL2 的记忆（`(title, x, y, w, h, flags)` 写反）会得到一串隐式转换错误。同样的坑还有
`SDL_ShowSimpleMessageBox(flags, title, message, window)`（SDL2 里 window 在第二位）。

### 4. `SDL_GetWindowDisplayScale` 不是 DPI 缩放比

两个名字很像，语义完全不同：

| 函数 | 含义 | 本机取值 |
|---|---|---|
| `SDL_GetWindowDisplayScale` | 系统“显示缩放”设置（Windows 的 125%/150%） | `1.25` |
| `SDL_GetWindowPixelDensity` | 可绘制像素 / 窗口逻辑尺寸 | `1.00` |

`IWindow::dpiScale()` 必须是**后者**：它的用途是把逻辑坐标换算成帧缓冲像素，
必须满足 `width() == round(logicalWidth * dpiScale())`。用前者会让测试立刻失败
（400 逻辑宽配 1.25 会期望 500 像素，实际 400）。

系统显示缩放（内容缩放）是另一件事，将来做 UI 缩放时再单独暴露一个访问器。

### 5. Windows 上 `rename` 不能覆盖已存在的文件

`std::filesystem::rename` 在 Windows 上目标存在时直接失败（POSIX 语义是覆盖）。
原子写因此需要三步：写 `.tmp` → `rename` → 失败则 `remove(target)` 后再 `rename`。
任一步失败都要清理临时文件，否则目录里会留下 `save.json.tmp`，
`listDirectory` 的断言马上会抓到。

### 6. UTF-8 路径不要经过 `u8path` / locale

统一用 `char8_t` 逐字节转换，避开已废弃的 `std::filesystem::u8path` 和依赖 locale 的 `path(std::string)`：

```cpp
std::filesystem::path toPath(std::string_view utf8);   // char -> char8_t -> path
std::string fromPath(const std::filesystem::path& p);  // path::u8string() -> char
```

测试用中文路径 + 中文内容（`温室的存档.txt`）覆盖这条路径。工程已带 `/utf-8`，
源文件里的中文字面量也是安全的。

### 7. 隐藏窗口的像素尺寸可能还没反映 DPI

刚创建、尚未显示的窗口，`SDL_GetWindowSizeInPixels` 报告的尺寸等于逻辑尺寸，
`SDL_GetWindowPixelDensity` 返回 1.0；窗口真正显示到高 DPI 显示器上之后才变成非 1.0。
因此 `width()/height()` **不能缓存**，渲染层每帧查询，并在
`SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED` 之后重新取尺寸。

### 8. 自写的悬垂指针

`SDL_SetWindowTitle` 只要求 `const char*`，写成
`const char* text = std::string(title).c_str();` 会让临时 `std::string` 立刻销毁。
所有传给 C API 的字符串都先落到具名 `std::string` 局部变量里再取 `.c_str()`。

## 遗留风险

| 项目 | 风险 | 说明 |
|---|---|---|
| `SDL_Quit` 是全局状态 | 中 | `SdlPlatform` 析构即调 `SDL_Quit`，因此同一进程内只能有一个 `IPlatform` 存活；将来做编辑器多窗口时需引入引用计数 |
| 无显示设备的 CI | 中 | 窗口用例已降级为 `WARN` 跳过，但 `SDL_Init(SDL_INIT_VIDEO)` 失败会让窗口相关断言完全不执行，覆盖率下降 |
| GL 上下文归 M06 | 低 | 已定：M02 只提供 `nativeHandle()`，SDL 的 `SDL_WINDOW_OPENGL` 标志已在创建时带上，WGL 上下文由 M06 建 |
| SDL3 API 演进 | 低 | 依赖 baseline 锁版本；只用 `SDL3staticTargets.cmake` 里列出的稳定接口 |
| 事件覆盖面 | 低 | 手柄事件未进入 `PlatformEvent`（需求是 v1 不用手柄），但 SDL 侧已初始化 GAMEPAD 子系统，M07 接上时只需扩展映射 |
| `showMessageBox` 阻塞 | 低 | 模态对话框会卡住事件泵，只在启动失败等场景使用 |

## 复现验证

```powershell
$env:VCPKG_ROOT = 'D:\CPPLibraries\vcpkg'
$msbuild = 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'

foreach ($c in 'Debug','Development','Release') {
    & $msbuild FloriansAdventure.sln /t:Rebuild /p:Configuration=$c /p:Platform=x64 /v:minimal
}

& .\build\bin\x64\Development\fa_app.exe --frames 3     # 隐藏窗口冒烟，退出码 0
& .\build\bin\x64\Development\fa_app.exe --window       # 可见窗口，手动关闭
& .\build\bin\x64\Development\fa_tests.exe              # 30 个用例全绿
```

`fa_app` 的参数：`--window` 打开可见窗口，`--frames N` 跑 N 帧后退出（默认隐藏窗口跑 3 帧）。
