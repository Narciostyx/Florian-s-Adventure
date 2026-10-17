# M00 构建骨架：实测结果与遗留风险

本文件记录 M00 在真实机器上的验证结果，以及搭建过程中踩过、已经修掉的坑。
新加入的协作者遇到构建问题时，按本文的“已知坑”逐条对照，通常能直接定位。

## 实测环境

| 项 | 值 |
|---|---|
| Visual Studio | Community 2026，18.10.12224.181 |
| MSVC 工具集 | 14.51.36231（`v145`） |
| MSBuild | `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe` |
| vcpkg | `D:\CPPLibraries\vcpkg\vcpkg.exe`（`VCPKG_ROOT` 需每次显式设置） |
| triplet | `x64-windows-static-md` |
| vcpkg baseline | `84bab45d415d22042bd0b9081aea57f362da3f35`（发布标签 `2025.12.12`） |

## 验证结果

依赖安装：`vcpkg install --triplet x64-windows-static-md` 成功，产出
`vcpkg_installed/x64-windows-static-md/lib/{Catch2,glm,imgui,SDL3-static}.lib`。

构建：`MSBuild FloriansAdventure.sln` 在 **Debug / Development / Release × x64** 三套配置下
Rebuild 全部成功，**零 warning、零 error**，8 个目标全部产出到 `build/bin/x64/<Config>/`。

运行：

```text
fa_app.exe   → Florian's Adventure - engine v0.1.0 (M01 core)     exit = 0
fa_tests.exe → All tests passed (14626 assertions in 22 test cases) exit = 0
```

## 已知坑（均已修复，勿回退）

### 1. 编译选项必须写在 `ItemDefinitionGroup` 里

`LanguageStandard`、`Optimization`、`RuntimeLibrary`、`PreprocessorDefinitions`、
`ExceptionHandling`、`WarningLevel` 等属于 `ClCompile` 的元数据。写在普通
`<PropertyGroup>` 中会被 MSBuild **静默忽略**，没有任何警告。

典型症状：明明设了 C++20，却报

```text
error C2429: 语言功能 "嵌套命名空间定义" 需要编译器标志 "/std:c++17"
error C2039: "string_view": 不是 "std" 的成员
```

现在 `Directory.Build.props` 把所有 `ClCompile` 相关配置集中在 `<ItemDefinitionGroup>` 内。
新增编译选项时，必须加在同一个 `ItemDefinitionGroup` 里，不要新建 `PropertyGroup`。

### 2. 工具集不能写死，也不能不写

- 写死 `v143`：只装了 VS2026 的机器直接报 `MSB8020`。
- 完全不写 `PlatformToolset`：MSBuild 会回退到 `v100`，同样报 `MSB8020`。

现在改为按 `VisualStudioVersion` 自适应选择（18.x → `v145`，17.x → `v143`），
并提供 `FA_PlatformToolset` 覆盖点：

```powershell
MSBuild FloriansAdventure.sln /p:FA_PlatformToolset=v143
```

### 3. `$(ProjectName)` 在 `Directory.Build.props` 中为空

`IntDir` 曾用 `$(ProjectName)` 拼接，求值结果为空，中间目录退化成
`build\obj\\x64\...`，多个项目共享同一目录，触发：

```text
warning MSB8028: 中间目录包含来自另一个项目的文件...
```

已统一改用 `$(MSBuildProjectName)`。

### 4. 不要手写 SDL3 库名

`x64-windows-static-md` 产出的是 **`SDL3-static.lib`**。手写 `SDL3.lib` 会得到
`LNK1181`。vcpkg 的 manifest 集成会在注入 include/lib 路径时一并注入库名，
因此 `fa_platform.vcxproj` 里**不需要也不应该**有 `ItemDefinitionGroup`。

同理，`Directory.Build.props` 中不要手工拼 `AdditionalIncludeDirectories`，
让 vcpkg 注入即可。

### 5. Catch2 需要异常与 RTTI

引擎全局使用 `/EHs-c-` 与 `/GR-`，Catch2 v3 两者都需要。`tests/fa_tests.vcxproj`
必须逐项覆盖：

```xml
<ExceptionHandling>Sync</ExceptionHandling>
<RuntimeTypeInfo>true</RuntimeTypeInfo>
```

并链接 `Catch2.lib`，附加库目录设为 `$(SolutionDir)vcpkg_installed\$(VcpkgTriplet)\lib`。
测试入口自己写 `Catch::Session`（`tests/main.test.cpp`），不依赖 `Catch2WithMain`。

### 6. 单独构建某个 `.vcxproj` 时要显式传 `SolutionDir`

`Directory.Build.props` 依赖 `$(SolutionDir)` 计算输出目录。直接 `MSBuild x.vcxproj`
时该变量为空，加参数：

```powershell
MSBuild src\engine\core\fa_core.vcxproj /p:SolutionDir="$PWD\"
```

推荐始终构建 `.sln`，或使用 `scripts/build.ps1`。

### 7. Debug 配置必须链接 `Catch2d.lib`

vcpkg 的 `x64-windows-static-md` 同时产出 Release 与 Debug 两套库：

```text
vcpkg_installed/x64-windows-static-md/lib/Catch2.lib        # /MD
vcpkg_installed/x64-windows-static-md/debug/lib/Catch2d.lib # /MDd
```

只写 `lib` 目录 + `Catch2.lib` 时，Debug 构建虽然能编过，链接阶段会报一片：

```text
error LNK2038: 检测到"_ITERATOR_DEBUG_LEVEL"的不匹配项: 值"0"不匹配值"2"
error LNK2038: 检测到"RuntimeLibrary"的不匹配项: 值"MD_DynamicRelease"不匹配值"MDd_DynamicDebug"
```

`fa_tests.vcxproj` 因此按配置拆成两个 `ItemDefinitionGroup`：Debug 用
`debug\lib` + `Catch2d.lib`，其余配置用 `lib` + `Catch2.lib`。
新增任何需要链接第三方静态库的目标时，同样要按配置区分目录与库名。

## 遗留风险

| 项目 | 风险 | 说明 |
|---|---|---|
| CI 首次运行 | 中 | `.github/workflows/ci.yml` 尚未在 GitHub Actions 上实际跑过，Windows runner 的 VS 版本与 vcpkg 缓存路径需首次 push 后核对 |
| `imgui` 的 `sdl3-binding` 特性 | 低 | 本机安装成功，但不同 vcpkg baseline 下特性名可能变化 |
| 静态 CRT 与第三方库混用 | 低 | 统一使用 `x64-windows-static-md`，若新增库只提供 `/MT` 版本需单独评估 |
| MSVC 版本升级 | 低 | 工具集自适应目前只覆盖 17.x/18.x，更早或更晚的 VS 需补充映射 |

## 复现验证

```powershell
$env:VCPKG_ROOT = 'D:\CPPLibraries\vcpkg'
& "$env:VCPKG_ROOT\vcpkg.exe" install --triplet x64-windows-static-md

$msbuild = 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'
foreach ($c in 'Debug','Development','Release') {
    & $msbuild FloriansAdventure.sln /t:Rebuild /p:Configuration=$c /p:Platform=x64 /m /v:minimal
}

& .\build\bin\x64\Development\fa_app.exe
& .\build\bin\x64\Development\fa_tests.exe
```

预期：三配置零警告零错误；`fa_app` 打印引擎版本；`fa_tests` 输出 22 个用例全部通过。
