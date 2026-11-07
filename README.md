# Florian's Adventure

俯视角 2D 像素风的**种植养成 + 实时动作战斗**游戏，自研轻量引擎。

## 技术基线

| 项 | 选择 |
|---|---|
| 语言 | C++20 |
| IDE / 构建 | Visual Studio 2026 + 原生 MSBuild `.sln` / `.vcxproj` |
| 包管理 | vcpkg manifest 模式（`x64-windows-static-md`） |
| 平台 | Windows 10/11 x64 |
| 窗口 / 输入 / 音频 | SDL3 |
| 图形 | OpenGL 4.1 Core（自研 42 项 GL 函数表，不引 glad） |
| 数学 | glm |
| 图像解码 | stb_image（app 注册为 `IImageDecoder`）；纹理读 `assets/textures/**/*.png`，缺图回落 catalog 色块 |
| 数据格式 | nlohmann/json |
| 调试 UI | M09 即时模式 UI（不引 ImGui） |
| 测试 | Catch2 |
| 逻辑帧率 | 固定 60 Hz，渲染可变帧率 |
| 世界单位 | 1 世界单位 = 1 逻辑像素；默认瓦片 16×16 |

## 模块分层

| 层 | 职责 | 模块 |
|---|---|---|
| L0 基础设施 | 构建、基础工具、平台、事件/任务、时间循环 | M00 Build、M01 Core、M02 Platform、M03 Events & Jobs、M04 Time |
| L1 引擎服务 | 资源、渲染、输入、音频、UI、动画 | M05 Assets、M06 Rendering、M07 Input、M08 Audio、M09 UI、M10 Animation |
| L2 世界运行时 | 场景/ECS、地图、物理、存档 | M11 Scene/ECS、M12 Tilemap、M13 Physics、M14 Save |
| L3 玩法系统 | 内容库、玩家、交互、物品、种植、战斗、AI、时间天气、NPC、经济、流程 | M15–M25 |
| L4 工具 | 编辑器与调试工具 | M26 Editor、M27 Debug |

## 仓库结构

```text
Florian-s-Adventure/
├─ src/
│  ├─ engine/        # 27 个引擎/玩法静态库：core / platform / render / input /
│  │                 # audio / ecs / tilemap / physics / save / player / farming /
│  │                 # combat / ai / narrative / economy / flow / editor / debug ...
│  └─ app/           # 唯一 composition root 与 main（fa_app.exe）
├─ tests/            # Catch2 单元测试与集成测试（fa_tests.exe）
├─ assets/           # 纹理 PNG（textures/<类别>/）与字体图集（fonts/）
├─ data/             # content/entities/shops/audio/catalog/animations/skills/maps JSON
├─ config/           # game.json（窗口/UI 缩放/路径/玩法/体力参数）
├─ tools/            # 资源工具（字体图集烘焙等）
├─ docs/             # 文档：guide/ reference/ status/ architecture/ adr/ roadmap/
├─ saves/            # 运行期存档（git 忽略）
├─ build/            # MSBuild 输出与共享 .props（bin/obj 忽略）
├─ vcpkg.json
└─ FloriansAdventure.sln
```

## 构建

```powershell
$env:VCPKG_ROOT='D:\CPPLibraries\vcpkg'
$msbuild='C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'
& $msbuild FloriansAdventure.sln /p:Configuration=Debug /p:Platform=x64 /m /nologo /v:m
```

三配置：`Debug` / `Development` / `Release` × `x64`；产物在 `build\bin\x64\<Config>\`。
VS 中把 `fa_app` 设为启动项目后 **F5** 即以 `--play --window` 编译并调试。
运行时从 `data/`、`config/`、`assets/` 读取数据/配置/图片；任意文件缺失都会回落到内置默认值。

## 命令行选项

| 选项 | 用途 | 窗口 |
|---|---|---|
| （无参数） | 无头脚本烟测：跑几帧，打印 m02–m27 模块报告后退出 | 隐藏 |
| `--help` / `-h` | 打印选项说明并退出 | — |
| `--play` | 正式游戏（写 `data/log/Log_<时间戳>.txt`） | 默认有 |
| `--debug` | 正式游戏 + 调试叠加默认开启（F3/F5）+ Debug 级日志 | 默认有 |
| `--play-test` | 无头玩法自检：新游戏、种植、战斗、传送、死亡、存档闭环 | 隐藏 |
| `--window` | 强制开窗口（任意自动化模式） | 有 |
| `--frames N` | 跑 N 帧后退出 | 隐藏 |
| `--resolution WxH` | 覆盖 `config/game.json` 的窗口尺寸 | — |
| `--soak SECONDS` | 浸泡测试：周期性存/读档并检查实体/纹理泄漏 | 隐藏 |
| `--stress N` | 压力基线：N 精灵场景跑 600 帧，输出 p50/p99 帧时间 | 隐藏 |

演示地图（`data/maps/farm.json`、`farm-scripted.json`、`forest.json`）与其中的脚本化实体
只服务自动化模式；`--play` 使用 `data/maps/play/`。

## 键位映射

| 输入 | 动作 |
|---|---|
| `WASD` | 移动（八向朝向，四向帧动画；静止时呼吸待机动画） |
| `Shift` | 奔跑，持续消耗体力 |
| `Space` | 闪避/冲刺，消耗一次体力 |
| `1`–`9` / `0` / 滚轮 | 选择快捷栏（锄头、浇水壶、种子、汤、小船、木剑等） |
| 左键 / `G` | 使用与交互（等效）：手持武器→近战挥砍；否则按优先级：村长/床/出货箱/商人交互 → 船（面向水域上船、靠岸下船）→ 食物进食 → 农具耕作 |
| 右键 | 武器技能·剑气（仅手持武器；弹体沿朝向飞行，伤害=武器×1.5） |
| `E` | 角色技能·瞬移（朝向前方 64px 内最远空位，带光环特效） |
| `I` | 打开背包：分类筛选 + 翻页，背包与快捷栏互相转移 |
| `J` | 打开任务日志 |
| `F2` | 地图编辑器（瓦片/对象/传送门/实体 四种 scope；瓦片单击查看、双击替换） |
| `Esc` | 关闭面板 / 暂停（继续/保存/存档槽/主菜单） |
| `F3` / `F4` / `F5` | 统计+面向瓦片面板 / 日志 / 物理调试（默认关闭，`--debug` 启动即开） |
| `` ` `` | 调试控制台（`help`、`time.addDays`、`time.set`、`give`、`teleport`；中文输入法兼容） |

数值配置：体力在 `config/game.json` 的 `stamina` 段；武器伤害/食物效果在 `data/content.json`；
技能参数在 `data/skills.json`；动画参数在 `data/animations.json`。
