# 技术栈

## 1. 总览

| 类别 | 选择 | 说明 |
|---|---|---|
| 语言 | C++20 | 使用 concepts、`std::span`、`std::string_view`、`<bit>`、三路比较 |
| IDE | Visual Studio 2026 | Desktop development with C++ |
| 构建 | 原生 MSBuild `.sln` / `.vcxproj` | 不使用 CMake 作为主构建；便于 VS 调试与 vcpkg 集成 |
| 包管理 | vcpkg manifest | `vcpkg.json` + `builtin-baseline` 固定版本 |
| 平台 | Windows 10/11 x64 | v1 只支持 Windows；平台层保留移植空间 |
| 窗口/输入/音频 | SDL3 | 通过 M02 封装；仅 M02 直接 include |
| 图形 | OpenGL 4.1 Core（自研 42 项函数表，不引 glad） | 通过 M06 封装；仅 M06 直接解析 GL 入口点，`SDL_GL_*` 仅 M02 的 `SdlGlContext` 使用 |
| 数学 | glm | 公开头可用；向量/矩阵/插值 |
| 图像 | stb_image / stb_image_write | 仅资源导入与调试截图使用 |
| JSON | nlohmann/json | 内容与存档；启用无异常解析路径 |
| 调试 UI | Dear ImGui | 仅 Debug/Development 与编辑器；Release 剔除 |
| 测试 | Catch2 | 单元测试与轻量集成测试 |
| 日志 | 自研薄封装（M01） | 分类/级别/线程安全；后续可替换 spdlog |
| 格式化 | clang-format | 随 VS 或 LLVM 安装；CI 检查 |
| 静态分析 | MSVC `/analyze` + clang-tidy（可选） | CI 先跑 `/analyze`，clang-tidy 逐步接入 |

## 2. 编译器与语言设置

### 引擎与玩法目标

```text
/std:c++20
/permissive-
/W4
/WX (CI)
/utf-8
/Zc:__cplusplus
/Zc:preprocessor
/EHs-c-        # 关闭 C++ 异常
/GR-           # 关闭 RTTI
/MP            # 多进程编译
```

- **异常**：引擎/玩法不使用异常；可恢复错误用 `Result<T>`；第三方库边界捕获并转换。
- **RTTI**：关闭；需要类型标识时使用显式 `TypeId`/枚举，不用 `dynamic_cast`。
- **警告**：`/W4`；CI 使用 `/WX`；第三方头用 `/external:I` 隔离警告。
- **字符集**：UTF-8 源文件与执行字符集；Windows API 使用宽字符转换封装在 M02。

### 第三方目标

- 使用 vcpkg 默认编译选项；不强制关闭异常/RTTI。
- 公共头中包含第三方头时，使用 `/external:I` 或 `#pragma warning(push)` 隔离。

## 3. 构建配置

| 配置 | 优化 | 断言 | 调试工具 | 热重载 | 用途 |
|---|---|---|---|---|---|
| Debug | `/Od` | 开 | ImGui + M27 | 开 | 日常开发、断点 |
| Development | `/O2` | 开 | ImGui + M27 | 开 | 性能测试、试玩 |
| Release | `/O2 /GL` | 关 | 剔除 | 关 | 发布 |

- 宏：`FA_DEBUG`、`FA_DEVELOPMENT`、`FA_RELEASE`；`FA_ENABLE_EDITOR`、`FA_ENABLE_DEBUG_TOOLS`、`FA_ENABLE_PROFILING`。
- 运行库：Debug 使用 `/MDd`，Release 使用 `/MD`；vcpkg triplet 使用 `x64-windows-static-md`（静态库 + 动态 CRT）作为默认，避免 DLL 分发问题。
- 链接：Release 开启 `/LTCG`；增量链接仅 Debug。

## 4. vcpkg manifest

目标 `vcpkg.json`（M00 落地时以实际可用版本为准）：

```json
{
  "name": "florians-adventure",
  "version-string": "0.1.0",
  "dependencies": [
    "sdl3",
    "glad",
    "glm",
    "stb",
    "nlohmann-json",
    "imgui",
    "catch2"
  ],
  "builtin-baseline": "<commit-sha>"
}
```

- `builtin-baseline` 固定到具体 commit，保证团队与 CI 一致。
- ImGui 的 SDL3/OpenGL3 backend 通过 vcpkg 特性或直接编译 `backends/` 源文件；在 M00 中确定唯一方案并写入项目文件。
- 不在仓库中提交 `vcpkg_installed/`；使用 `VCPKG_ROOT` 与 manifest 模式。
- 新增依赖必须：更新 `vcpkg.json`、记录许可证、在模块文档中说明用途、评估二进制体积。

## 5. 解决方案与项目结构

```text
FloriansAdventure.sln
├─ src/engine/core/fa_core.vcxproj              -> fa_core
├─ src/engine/platform/fa_platform.vcxproj      -> fa_platform
├─ src/engine/events/fa_events.vcxproj          -> fa_events
├─ src/engine/time/fa_time.vcxproj              -> fa_time
├─ src/engine/assets/fa_assets.vcxproj          -> fa_assets
├─ src/engine/render/fa_render.vcxproj          -> fa_render
├─ src/engine/input/fa_input.vcxproj            -> fa_input
├─ src/engine/scene/fa_scene.vcxproj            -> fa_scene
├─ src/engine/physics/fa_physics.vcxproj        -> fa_physics
├─ src/engine/tilemap/fa_tilemap.vcxproj        -> fa_tilemap
├─ src/engine/interaction/fa_interaction.vcxproj-> fa_interaction
├─ src/engine/player/fa_player.vcxproj          -> fa_player
├─ src/engine/content/fa_content.vcxproj        -> fa_content
├─ src/engine/ui/fa_ui.vcxproj                  -> fa_ui
├─ src/engine/inventory/fa_inventory.vcxproj    -> fa_inventory
├─ src/engine/audio/fa_audio.vcxproj            -> fa_audio
├─ src/engine/debug/fa_debug.vcxproj            -> fa_debug（仅非 Release 由 app 引用）
├─ src/engine/save/fa_save.vcxproj              -> fa_save
├─ src/engine/anim/fa_anim.vcxproj              -> fa_anim
├─ src/engine/combat/fa_combat.vcxproj          -> fa_combat
├─ src/engine/world/fa_world.vcxproj            -> fa_world
├─ src/engine/farming/fa_farming.vcxproj        -> fa_farming
├─ src/engine/ai/fa_ai.vcxproj                  -> fa_ai
├─ src/engine/editor/fa_editor.vcxproj          -> fa_editor（仅非 Release 由 app 引用）
├─ （待建）src/game/...                          -> 玩法模块（M23–M25）
├─ src/app/fa_app.vcxproj                       -> 可执行文件
├─ tests/fa_tests.vcxproj                       -> Catch2 测试
└─ tools/                                       -> 资源导入/校验（可选 C++ 或脚本）
```

- 每个模块一个静态库；`src/app` 是唯一可执行目标。
- 工程名统一小写下划线（`fa_scene`、`fa_render`），不叫 `FA.ECS` 那种点分大写名。
- 项目引用（ProjectReference）表达真实依赖；不靠「全仓库 include 路径」偷懒。
- 公共头集中在 `src/engine/include/fa/<module>/...`，私有源在 `src/engine/<module>/src/...`。
  这样一个模块的公共头只有一处，且 `#include "fa/scene/World.hpp"` 的形式与磁盘布局一一对应。
- 各工程的 `ClCompile` / `ClInclude` **显式列出文件**，不使用 `src\*.cpp` 通配符。VS2026 会对项目项定义里的通配符发出警告，并可能造成 IDE 行为不稳定；新增源文件时要同时改对应的 `.vcxproj`，新增工程才需要改 `.sln`。

## 6. 资源与内容流水线

```text
assets/raw/       原始 PNG/JSON/音频
   │  tools/import（可选，M05/M26）
   ▼
assets/build/     运行时资源（纹理图集、压缩音频、校验后的 JSON）
   │  构建事件复制
   ▼
build/<config>/assets/
```

- v1 可以先直接加载 `assets/` 下的 PNG/JSON，图集与压缩作为优化项。
- 所有内容 JSON 在加载时校验；CI 运行 `tools/validate_content` 检查重复 ID、缺失引用、非法数值。
- 大文件使用 Git LFS（音频、图集、字体）；阈值与规则在 M00 确定。

## 7. 测试策略

| 层级 | 工具 | 范围 |
|---|---|---|
| 单元测试 | Catch2 | M01/M03/M04/M11/M13/M14/M15/M18/M19/M20 的纯逻辑 |
| 集成测试 | Catch2 + 无窗口后端 | 场景加载、存档往返、内容校验 |
| 黄金测试 | JSON 快照 | 存档序列化、内容解析结果 |
| 冒烟测试 | 可执行文件 + 隐藏窗口 | 启动、创建窗口、加载资源、退出 |
| 性能基准 | 自研计时 + M27 | ECS 遍历、碰撞查询、精灵批处理 |

- 单元测试不得创建 OpenGL 上下文；渲染相关逻辑通过“绘制项列表”测试。
- 随机测试使用固定种子；失败时打印种子。
- CI 至少运行 Debug + Release 的测试目标。

## 8. CI（GitHub Actions）

```yaml
name: build
on: [push, pull_request]
jobs:
  windows:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4
      - name: Setup vcpkg
        run: |
          git clone https://github.com/microsoft/vcpkg.git C:/vcpkg
          C:/vcpkg/bootstrap-vcpkg.bat
      - name: Configure env
        run: echo "VCPKG_ROOT=C:/vcpkg" >> $env:GITHUB_ENV
      - name: Build
        run: msbuild FloriansAdventure.sln /m /p:Configuration=Release /p:Platform=x64
      - name: Test
        run: ./build/Release/FA.Tests.exe
```

- 实际落地时增加 vcpkg 二进制缓存（`VCPKG_BINARY_SOURCES`）与 `.vcxproj` 路径修正。
- PR 必须通过：构建、单元测试、内容校验、clang-format 检查。
- 夜间任务可选：`/analyze`、性能基准、许可证扫描。

## 9. 工具与编辑器

- **Visual Studio 2026**：主 IDE；使用 `.vcxproj` 调试、性能分析器、Git 集成。
- **VS Code**：可选，用于 Markdown/JSON 编辑；不维护第二套构建。
- **RenderDoc**：OpenGL 调试；仅开发期。
- **ImGui**：运行时调试 UI；M27 提供 ECS 检查器、性能面板、控制台。
- **clang-format**：`.clang-format` 在 M00 添加；CI 检查。
- **Git LFS**：大资源；在 M00 配置 `.gitattributes`。

## 10. 版本与升级策略

- vcpkg baseline 每季度评估一次升级；升级必须单独 PR，并跑完整测试。
- SDL3/OpenGL/glm 等核心依赖的大版本升级需要 ADR。
- 不随意引入新第三方库；优先标准库与现有依赖。
- 所有第三方库必须在 `docs/adr/` 或模块文档中记录用途与许可证。

## 11. 开放问题

1. VS2026 实际 MSVC 工具集版本与 `/std:c++20` 兼容性需在 M00 验证。
2. ImGui 的 SDL3/OpenGL3 backend 在 vcpkg 中的特性名与编译方式需在 M00 确认。
3. Release 是否使用静态 CRT（`x64-windows-static`）需评估 SDL3 与许可证。
4. 是否引入 `spdlog` 替代自研日志，待 M01 性能测试后决定。
