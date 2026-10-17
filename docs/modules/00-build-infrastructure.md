# M00 Build & Dependency Infrastructure

| 项 | 值 |
|---|---|
| 层 | L0 |
| 优先级 | P0 |
| 状态 | 设计完成 |
| 依赖 | 无 |
| 被依赖 | 全部模块 |

## 1. 目的

让任何一台安装了 Visual Studio 2026 的 Windows 机器，都能用一条命令获得可重复的构建结果；让依赖版本、编译选项、目录结构和 CI 成为**工程事实**，而不是口头约定。

## 2. 范围

**负责**

- `FloriansAdventure.sln` 与各模块 `.vcxproj` 的骨架、项目引用关系。
- `vcpkg.json`（manifest 模式）、triplet、`builtin-baseline`。
- `Directory.Build.props` / `Directory.Build.targets`：公共编译选项、宏、输出目录。
- `.gitignore`、`.gitattributes`、Git LFS 规则。
- `.clang-format`、`.editorconfig`。
- `scripts/build.ps1`、`scripts/test.ps1`、`scripts/format.ps1`。
- GitHub Actions 构建/测试流水线。
- 目标目录骨架：`src/engine`、`src/game`、`src/app`、`assets`、`tests`、`tools`。

**不负责**

- 具体模块的代码与接口（由各模块负责）。
- 资源导入工具的具体实现（M05/M26）。
- 安装程序、签名、发布打包（后续）。

## 3. 核心概念

- **配置矩阵**：`Debug` / `Development` / `Release` × `x64`。
- **目标图**：每个模块一个静态库；`src/app` 是唯一 exe；`tests` 是唯一测试 exe。
- **公共头边界**：每个项目只导出自己的 `include/`，私有 `src/` 不进入其他项目的 include 路径。
- **依赖锁定**：vcpkg baseline + `vcpkg.json`；不提交 `vcpkg_installed/`。
- **构建宏**：`FA_DEBUG`、`FA_DEVELOPMENT`、`FA_RELEASE`、`FA_ENABLE_EDITOR`、`FA_ENABLE_DEBUG_TOOLS`。

## 4. 公共接口草案

M00 没有 C++ 运行时接口；它提供构建入口：

```powershell
# scripts/build.ps1
param([string]$Config = "Debug")
msbuild FloriansAdventure.sln /m /p:Configuration=$Config /p:Platform=x64

# scripts/test.ps1
param([string]$Config = "Debug")
./build/$Config/FA.Tests.exe
```

`Directory.Build.props` 关键内容：

```xml
<PropertyGroup>
  <LanguageStandard>stdcpp20</LanguageStandard>
  <CharacterSet>Unicode</CharacterSet>
  <MultiProcessorCompilation>true</MultiProcessorCompilation>
  <OutDir>$(SolutionDir)build\$(Configuration)\</OutDir>
  <IntDir>$(SolutionDir)build\obj\$(ProjectName)\$(Configuration)\</IntDir>
</PropertyGroup>
```

## 5. 依赖关系

无上游依赖。所有模块通过项目引用依赖 M00 定义的公共属性；M00 不链接任何模块。

## 6. 扩展点

- 新增模块：添加 `.vcxproj`，在解决方案中建立项目引用，更新目标图。
- 新增第三方库：修改 `vcpkg.json`，更新 baseline 说明与许可证记录。
- 新增配置：在 `Directory.Build.props` 增加宏与优化选项，并更新 CI 矩阵。
- 新增平台：增加 triplet 与平台层后端；不改业务模块。

## 7. 实现阶段

P0，第一个实现。验收：

1. 全新克隆后按 README 步骤可构建空 exe。
2. `scripts/build.ps1 -Config Debug` 与 `Release` 均成功。
3. CI 在 PR 上跑通构建与空测试。
4. 依赖扫描脚本能发现反向 include。

## 8. 风险与开放问题

- VS2026 实际 MSVC 工具集版本与 vcpkg 的兼容性需尽早验证。
- ImGui 的 SDL3/OpenGL3 backend 在 vcpkg 中的特性名/编译方式未最终确定。
- Release 使用 `x64-windows-static-md` 还是 `x64-windows-static` 需评估 SDL3 分发与许可证。
- 是否需要为 `tools/` 增加独立的 C++ 工具项目，或先用 Python/PowerShell 脚本。
