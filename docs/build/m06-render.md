# M06 渲染：实现说明、实测结果与遗留风险

本文件记录 M06（Rendering）的落地细节：真实接口形态、本机实测结果，
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
| GPU / 驱动 | AMD Radeon (TM) Graphics，OpenGL 4.1.0 Core Profile Context 23.19.260413 |

## 实测结果

三套配置（Debug / Development / Release × x64）构建全部成功，**零 warning、零 error**。

```text
fa_tests.exe → All tests passed (16780 assertions in 154 test cases)   exit = 0
fa_tests.exe "[render]"
             → All tests passed (182 assertions in 42 test cases)       exit = 0
fa_app.exe --frames 60
             → m06: 4.1.0 Core Profile Context 23.19.260413 on AMD Radeon (TM) Graphics
                    2 draw call(s), 966 quad(s), 3864 vertex/vertices,
                    1 texture switch(es), 0 dropped, 2 live texture(s)
               M02+M03+M04+M06+M07 smoke test OK                        exit = 0
```

`fa_tests` 的 154 个用例 = M01 的 22 个 + M02 的 8 个 + M04 的 20 个 + M03 的 26 个
+ M07 的 36 个 + M06 新增 42 个。三配置运行结果一致。

`m06:` 行在三配置下**逐字相同**，且连跑 3 次不变。

### 关键未知数：隐藏窗口能不能拿到 GL 上下文

开工前这是 M06 最大的未知数——CI 与无头冒烟都依赖它。**结论：能。**
在隐藏窗口（`SDL_WINDOW_HIDDEN`）上 `SDL_GL_CreateContext` 成功返回了一个 4.1 Core 上下文，
AMD Radeon 驱动在无可见 surface 时依然提供了上下文。两次运行的区别只有 vsync：

| 运行方式 | vsync | 帧率 | 备注 |
|---|---|---|---|
| `fa_app --frames 60`（隐藏） | 0 | 60.0 fps（软件节拍） | 无头/CI 路径 |
| `fa_app --frames 60 --window` | 1 | 60.8 fps（vsync 驱动） | 真实窗口路径 |

即便如此，代码**仍然按「可能失败」写**：隐藏窗口初始化失败只打印一行并继续，
可见窗口失败才返回 1。理由是「本机能拿到」不等于「CI 镜像能拿到」——
GitHub 的 Windows runner 走的是另一套图形栈。

### 966 个 quad 是怎么来的

冒烟帧提交的内容是完全确定的：

| 内容 | 数量 |
|---|---|
| 地面瓦片（24 行 × 40 列，与 `kTileSize = 16` 对齐） | 960 |
| 鼠标位置的 1 个标记精灵 | 1 |
| `drawDebugRect` 的四条边 | 4 |
| `drawDebugLine` 的一条 | 1 |
| **合计** | **966** |

2 个 draw call = 「棋盘纹理一批」+「白纹理一批」；1 次纹理切换 = 两者之间那一次。
这行数字同时验证了批处理、排序、debug 图元复用同一批、以及统计口径。

## 交付内容

### 公共头（`src/engine/include/fa/render/`）

| 文件 | 内容 |
|---|---|
| `RenderConstants.hpp` | `kVirtualWidth = 640`、`kVirtualHeight = 360`、`kTileSize = 16`、`kDefaultMaxQuads = 4096`、`kMaxTextures = 4096`、`kMaxTextureSize = 16384` |
| `RenderError.hpp` | `render_error` 4000–4005（M06 初版误用 5000–5005，与 Ecs/Physics 区间冲突，M05 期间一并改正） |
| `RenderTypes.hpp` | `TextureHandle`、`TextureFormat`、`bytesPerPixel`、`TextureDesc`、`RenderLayer`、`DrawItem`、`depthKey`、`drawsBefore`、`RenderStats` |
| `Viewport.hpp` | `Viewport`：整数缩放 + letterbox + 双向坐标换算 |
| `Camera2D.hpp` | `Camera2D`：只平移、边界夹取、死区/平滑/限速跟随、正交投影矩阵 |
| `RenderQueue.hpp` | `RenderQueue` + 静态 `countDrawCalls` / `countTextureSwitches` |
| `IRenderer.hpp` | `RendererDesc`、`IRenderer`、`createRenderer()` |

### 公共头（`src/engine/include/fa/platform/`）

| 文件 | 内容 |
|---|---|
| `GlContext.hpp` | `GlContextDesc`、`IGlContext` |

### 私有实现

| 文件 | 内容 |
|---|---|
| `src/engine/render/src/RenderTypes.cpp` | `toString` 两张名字表、`effectiveRowPitch` |
| `src/engine/render/src/Viewport.cpp` | `fit` 与坐标换算 |
| `src/engine/render/src/Camera2D.cpp` | 边界夹取、`follow()` 死区/指数平滑/限速、`projection` 手写列主序矩阵 |
| `src/engine/render/src/RenderQueue.cpp` | `stable_sort` + 两次遍历统计 |
| `src/engine/render/src/GlApi.hpp` | **私有头**：GL 类型/常量、`FA_GL_FUNCTION_LIST(X)`（42 个入口点）、`GlFunctions` |
| `src/engine/render/src/GlApi.cpp` | 逐名解析 + `takeGlError` |
| `src/engine/render/src/GlRenderer.cpp` | `GlRenderer final : IRenderer`，唯一实现 |
| `src/engine/platform/src/SdlGlContext.hpp` / `.cpp` | `IGlContext` 的 SDL 实现 |

### 辅助改动

| 文件 | 改动 |
|---|---|
| `src/engine/include/fa/platform/Platform.hpp` | include `GlContext.hpp`；`IPlatform` 新增纯虚 `glContext()` |
| `src/engine/include/fa/platform/PlatformError.hpp` | 新增 `kGlContextCreationFailed = 2008`、`kGlUnsupported = 2009` |
| `src/engine/platform/src/SdlPlatform.hpp` / `.cpp` | 新增 `SdlGlContext m_glContext`；`shutdown()` 先销毁上下文再销毁窗口 |
| `tests/fa_tests.vcxproj` | 引用 `fa_render` |
| `tests/time.test.cpp` | `FakePlatform` 补 `glContext()`（见坑 #3） |
| `src/app/main.cpp` | `SmokeSink` 集成渲染器与 `m06:` 行 |

`fa_app.vcxproj` 在 M00 骨架期就已经引用了 `fa_render`，本轮无需改动。
`fa_render` 只引用 `fa_core` + `fa_platform`，**不引用任何上层模块**，也不引用 `fa_assets`（纹理上传由 app 的 `ITextureUploader` 适配器桥接）。
它**不需要 `/EHsc`**：不 include `nlohmann/json`，也就不会拉进 `<chrono>`。

### 删除

`src/engine/render/src/Renderer.cpp` 是 M00 骨架期的占位实现，
自己定义了一套与 `RenderConstants.hpp` 同名的常量，两者编进同一个工程必然重定义。
全仓库无人引用它，已整份删除，由 `GlRenderer.cpp` 取代。

## 语义决策

| 决策 | 选择 | 原因 |
|---|---|---|
| GL 上下文归属 | `fa_platform::IGlContext`，由 `IPlatform::glContext()` 暴露 | 上下文寿命跟着窗口走；`fa_render` 因此零 SDL 依赖，换后端只需换 `IGlContext` |
| GL 函数解析 | 自研 X-macro 表（42 个入口点），不引 glad | 零新依赖；这张表本身就是「引擎用到的 GL 面」的显式清单，加一个调用必须同时改两处 |
| `Camera2D` 缩放 | **没有** zoom 字段，只平移 | 像素画缩放系数一旦非整数，精灵边缘就落到半像素；放大窗口是 `Viewport` 的整数倍职责 |
| `Camera2D` 跟随 | 死区 + 指数平滑 + 可选限速，边界每步夹取 | 死区避免走路时画面抖动；指数平滑与帧率无关；夹取保证 128×128 地图边缘不露空白 |
| 跟随收敛 | 最后 1e-4 px 直接吸附 | 纯指数逼近永远到不了边界，会让相机的 clamp 看起来「差一点」 |
| `Viewport::scale` 下限 | 1 | 窗口比 640×360 还小时宁可裁剪边缘，也不缩到 1:1 以下（糊比丢更糟） |
| letterbox 实现 | 两次 `glClear`（先整窗黑、再 `glViewport` 后清虚拟画布） | 不用 scissor，省一次状态切换；两块区域天然不重叠 |
| letterbox 颜色 | 永远是黑 | `RendererDesc::clearColor` 只描述虚拟画布，黑边是「画布之外」而不是内容 |
| `sortY` 量化 | 四舍五入（`depthKey`） | 同一行站两个角色时，浮点差异不该让它们每帧互换前后；四舍五入对负数也对称 |
| 排序稳定性 | `std::stable_sort` | `(layer, depth, entityId)` 仍然可能三者全相等，此时保留提交顺序才可复现 |
| 深度/模板缓冲 | 关闭 | 2D 渲染器不用；开着白占显存 |
| 批处理断点 | 只有「纹理变了」 | v1 只有一种混合状态；多断点留到加混合模式时 |
| 调试图元 | 与精灵共用批 + 1×1 白纹理 | 免第二条管线；`drawDebugRect` 常常紧邻同纹理图元，通常不多花 draw call |
| 纹理句柄 | `StrongId`（槽位下标 + 1），不含指针 | 旧句柄最多被报成 `kUnknownTexture`，永远不会被解引用 |
| `TextureDesc` 校验 | 结构体不校验，`createTexture` 校验 | 描述是纯数据；合法性检查只有一个执行者，避免两处判定不一致 |
| 像素吸附位置 | 只在生成顶点时（`worldToPixel`/`snapped`） | 模拟状态保留亚像素位置，移动速度才不会被吸附成阶梯 |
| 每帧容量 | 超限的 `submit` 被拒并计入 `droppedItems` | 宁可少画也不崩；统计让「丢了」可观测 |
| 隐藏窗口初始化失败 | 非致命，打印 `renderer unavailable` 继续 | 无头/CI 不一定有 GL 驱动；冒烟的目的是验证接线 |
| 可见窗口初始化失败 | 返回 1 | 玩家要看到的窗口画不出东西就是真故障 |
| 关闭顺序 | 先 `renderer.shutdown()`，再 `platform->shutdown()` | 上下文绑在窗口上，窗口先没了上下文就悬空 |
| 冒烟纹理来源 | 程序化 16×16 棋盘 | M05（资源加载）未实现；上传路径本身依然被测到 |
| 文字绘制 | v1 不做 | 字体图集、字形缓存、排版都归 M09；`IRenderer` 保持窄 |

## 踩到的坑

### 1. SDL3 的 `SDL_GLContext` 不是 `void*`（C2664）

`SdlGlContext` 最初把上下文存成 `void* m_context`，跟着 SDL 老版本的印象写。
SDL3 实际定义是：

```c
typedef struct SDL_GLContextState *SDL_GLContext;   // SDL_video.h
```

于是 `SDL_GL_MakeCurrent(window, m_context)` / `SDL_GL_DestroyContext(m_context)`
在函数签名为 `SDL_GLContext` 的参数处报 C2664：`void*` 不能转换成
`struct SDL_GLContextState *`。

**修法**：私有头里前置声明 tag，成员用真实类型：

```cpp
struct SDL_GLContextState;              // 不 include SDL_video.h，避免头污染
SDL_GLContextState* m_context = nullptr;
```

前置声明的好处是 `SdlGlContext.hpp` 仍然不需要 include `<SDL3/SDL.h>`——
只有 `.cpp` 需要。这也顺带守住了「私有头尽量少暴露第三方类型」这条习惯。

### 2. 跨目录相对 include（C1083）

`RenderTypes.cpp` 里残留了一行 `#include "RenderConstants.hpp"`。`RenderTypes.cpp`
在 `src/engine/render/src/`，而 `RenderConstants.hpp` 在 `include/fa/render/`，
只有工程 include 路径能解析 `<fa/render/...>` 形式，引号形式会按「同目录优先」找不到：

```text
error C1083: 无法打开包括文件: "RenderConstants.hpp"
```

**修法**：删掉这行（该文件根本没用到任何常量）。同时钉住一条规则：
**公共头一律用 `<fa/render/...>` 尖括号形式，只有同目录私有头（`GlApi.hpp`）用引号。**
`RenderTypes.cpp` 本来也不需要常量——它就是被 `RenderConstants.hpp` 的读者 include 的那一头。

### 3. `IPlatform` 加纯虚函数，连带打断所有测试替身（C2259）

`IGlContext` 暴露给渲染器的最自然入口是 `IPlatform::glContext()`，于是给接口加了纯虚函数。
`IPlatform` 的 fake 不止一个真实实现——`tests/time.test.cpp` 里的 `FakePlatform`
也想当然地实例化失败：

```text
error C2259: "FakePlatform": 无法实例化抽象类
```

**修法**：给 fake 补一个最小实现：

```cpp
fa::platform::IGlContext* glContext() override { return nullptr; }
```

这条要记住的不是「补一行」而是**接口加纯虚的连锁半径**：`IPlatform` 是 L0 接口，
每个 fake 都会断。加纯虚前先 `grep` 一遍 `: public IPlatform` 更省事。

### 4. 哨兵值 `Count` 被 `toString` 落到 `"Unknown"`

`[render]` 里原本写了 `CHECK(toString(RenderLayer::Count) == "Count")`，实测是 `"Unknown"`。
`toString` 的 switch 用 `default:` 接未知值，而 `Count` 是**哨兵**而不是合法层，
所以走 `default` 返回 `"Unknown"`。`TextureFormat::Count` 同理。

**修法**：改断言（`"Unknown"` 就是正确语义）。教训：给枚举加 `Count` 时，
「它是不是合法值」必须在 `toString` 的语义里说清楚，测试要断言**哨兵的行为**而不是编一个名字出来。

### 5. `effectiveRowPitch()` 对显式行距原样返回

原本断言「显式 `rowPitchBytes` 小于一行时会被纠正」，实测原样返回。
设计选择是：**描述结构不校验，执行者校验**（见语义决策表），非法值由
`GlRenderer::createTexture` 拒绝并返回 `kTextureCreationFailed`。

**修法**：断言改成「原样返回」，并在用例名里写明「校验在渲染器那侧」。

### 6. 360 不是 16 的整数倍（`360 % 16 == 8`）

写用例时顺手断言「`kVirtualHeight` 能被 `kTileSize` 整除」，结果：

```text
kVirtualHeight = 360, kTileSize = 16 → 360 / 16 = 22 余 8
```

16:9 的 640×360 在 16 像素瓦片下纵向只有 **22.5 格**。这不是 bug，是像素基准
（ADR-0003）选 16:9 的必然结果，但它会实实在在影响 M10/M12 的地图布局。

**修法**：断言改成 `kVirtualHeight / kTileSize == 22` 且 `% kTileSize == 8`，
并在用例注释里标注「M10 按 22 行算，或改用 640×352」。改数值前先想清楚：
换 352 就变成 16:11，不再「像」16:9。

> 以上三条（#4/#5/#6）的共同教训：**新模块的用例写的是「我以为的语义」，
> 第一次跑测试就是拿实现给设计回话。** 三处全部是断言错、实现对，所以修的是用例；
> 反过来说，如果当时为了让测试过而去改实现，就会把三条正确语义一起改坏。

### 7. `reinterpret_cast<void*>` → 函数指针在 MSVC 下不报警（正向记录）

用函数指针表代替 glad，最担心的是 `/W4 /WX` 会不会因为
「对象指针转函数指针」报 C4191 之类。实测：`SDL_GL_GetProcAddress` 返回
`SDL_FunctionPointer`，`IGlContext::procAddress` 转成 `void*`，再
`reinterpret_cast<decltype(&glXxx)>` 转成函数指针，**在 MSVC 14.51 的 `/W4 /WX` 下零警告**。

记录这条是因为它决定了「自研函数表」这条路可不可行——可行，但这是**平台相关的善意**：
C++ 标准里对象指针与函数指针互转是实现定义行为，换到 GCC/Clang 可能有别的诊断。
真要做跨编译器版本，把 `procAddress` 的返回类型改成专用的 `using GlProc = void (*)();`
更干净。

### 8. M00 占位实现与正式常量撞名

M00 骨架期为 `render/src/Renderer.cpp` 提交过一个占位实现，里面有自己的一套
「虚拟分辨率/瓦片尺寸」常量，与正式的 `RenderConstants.hpp` 同名不同值。
两者编进同一个静态库必然重定义，而且在排查真正的编译错误时它会先冒出来干扰。

**修法**：整份删除（全仓库无人引用）。这条和 M07 删掉 `InputSystem.cpp` 占位实现是同一类：
**骨架期的占位实现一旦有了正式版本，就应当在同一批改动里删掉**，
否则它会在很久以后以「重定义」的面目回来找你。

## 复现命令

```powershell
$env:VCPKG_ROOT = 'D:\CPPLibraries\vcpkg'
.\scripts\build.ps1 -Configuration Development -NoInstall
.\build\bin\x64\Development\fa_tests.exe
.\build\bin\x64\Development\fa_tests.exe "[render]"
.\build\bin\x64\Development\fa_app.exe --frames 60
.\build\bin\x64\Development\fa_app.exe --frames 60 --window
```

三配置逐字比对 `m06:` 两行：

```powershell
foreach ($cfg in 'Debug', 'Development', 'Release') {
  .\build\bin\x64\$cfg\fa_app.exe --frames 60 | Select-String 'm06:|draw call'
}
```

若只想确认「有没有拿到上下文」，看 `m06:` 第一行的 `GL_VERSION` 是否非空即可；
拿不到时会打印 `m06: renderer unavailable (no GL context)` 且冒烟**仍然通过**。

## 全新克隆验证

针对提交 `bb50916`（`feat(render): implement M06 rendering with OpenGL 4.1 sprite batching`）
做了一次全新克隆：

```powershell
git clone --local . --no-hardlinks "$env:TEMP\fa-clone-m06"
cd "$env:TEMP\fa-clone-m06"
$env:VCPKG_ROOT = 'D:\CPPLibraries\vcpkg'
.\scripts\build.ps1 -Configuration Development      # 含空 vcpkg_installed 的 manifest 安装
.\build\bin\x64\Development\fa_tests.exe
.\build\bin\x64\Development\fa_app.exe --frames 60
```

```text
fa_tests.exe → All tests passed (16780 assertions in 154 test cases)   exit = 0
fa_app.exe   → ran 60 frame(s) in 0.999s
                 average: 60.0 fps
                 m06: 4.1.0 Core Profile Context 23.19.260413 on AMD Radeon (TM) Graphics
                        2 draw call(s), 966 quad(s), 3864 vertex/vertices,
                        1 texture switch(es), 0 dropped, 2 live texture(s)
               M02+M03+M04+M06+M07 smoke test OK                        exit = 0
```

新增的 `include/fa/render/`、`include/fa/platform/GlContext.hpp`、`render/src/`、`platform/src/SdlGlContext.*`
与 `tests/render.test.cpp` 在克隆里都正常参与构建，说明没有文件被 `.gitignore` 误伤
（M02 曾因 `/build/` 把 `build/*.props` 一起忽略而踩过这个坑）。

> 注：克隆到 `%TEMP%` 下会额外出现 `MSB8029`（中间目录位于临时目录），
> 这是存放位置的副作用，与本仓库配置无关；正常路径下无此警告。

## 相机跟随（v1.1 补充）

- `CameraFollowConfig { deadzone, smoothing, maxSpeed }`；`smoothing = 0` 为硬跟随。
- `follow(target, dt)` 用死区把目标保持在中心附近，再按指数平滑靠近；每步
  `setPosition()` 继续执行地图边界夹取。
- 128×128 瓦片地图（2048×2048 世界）测试覆盖：逐格跟随、视野不越界、四角夹取。
- app 在 `renderFrame()` 里以玩家精灵中心（`markerCenter()`）为目标，死区 48×32、
  平滑 10/s，冒烟摘要额外打印 `camera (...)` 与跟随参数。
- 仍未做：像素对齐的整数吸附跟随（渲染端已 `snapped()`，相机位置仍保留亚像素）、
  相机前瞻/回拉、震动、过场轨道。

## 遗留风险

| 风险 | 影响 | 处理 |
|---|---|---|
| GL 常量值只在编译期核对过 | 某个 `GL_*` 数值写错 → 黑屏或纹理格式错 | 42 个入口点已在本机（AMD / GL 4.1）实测点亮；换驱动或换平台时先看 `m06:` 行 |
| 隐藏窗口依赖驱动 | 无 GL 驱动的 CI 镜像拿不到上下文 | 隐藏窗口初始化失败被降级为非致命；`m06:` 行会打 `renderer unavailable`，冒烟仍通过 |
| 上下文创建失败的原因被压成两个错误码 | `kGlContextCreationFailed` / `kGlUnsupported` 分不清「属性被拒」与「驱动不支持」 | 失败时 `logf` 已带 SDL 的 `GetError()` 文本；需要更细时再拆码 |
| 句柄槽位回收 | 销毁后的旧句柄可能命中新纹理 | 契约禁止销毁后继续持有；真要抓就给 `TextureHandle` 加代际计数 |
| 非 90° 旋转破坏像素风 | 旋转精灵边缘模糊 | `DrawItem::rotation` 注释里标了「只用于特效」，无强制 |
| 单一混合模式 | 预乘 alpha、加色、遮罩都做不了 | 分批断点位置已留好，加混合状态只是多一个断点字段 |
| 无渲染目标 / 后处理 | 做不了光照、屏幕扭曲、缩放特效 | `GlContextDesc` 已预留 depth/stencil 开关；render target 走新增 `RenderPass` |
| 无文字绘制 | 任何文字（含调试文字）都画不出来 | v1 故意；`drawText` 与字体图集归 M09，`IRenderer` 保持窄 |
| 每帧一次 `stable_sort` | 上万绘制项时排序开销可见 | 每帧上限 4096 quad；真到瓶颈按层分桶（层内通常已近似有序） |
| `kVirtualHeight % kTileSize != 0` | 纵向 22.5 格 | M10/M12 按 22 行布局，或改用 640×352（但那就不再是 16:9） |
| 单批上限 16384 quad | `maxQuadsPerFrame` 请求被夹到上限 | 夹取发生在 `initialize()`，`droppedItems` 可观测；正常帧远低于此 |
| 程序化纹理只覆盖 RGBA8 | `R8` 路径没有实机用例 | `R8` 的枚举、`bytesPerPixel` 与上传分支都在；M05 接入真实资源时补一条用例 |
