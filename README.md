# Florian's Adventure

一款俯视角 2D 像素风的**种植养成 + 实时动作战斗**游戏。

> 当前状态：**28 个模块已实现并通过三配置实测**。`--play` 可进入可操作会话：
> 主菜单/暂停/存档槽、种植、近战/掉落、睡眠日终、棋盘与商店、
> 农场↔林地独立地图切换（`SceneStack::replace`）、Elder 对话/任务/奖励、
> 体力消耗（奔跑/闪避/攻击/工具，JSON 可配）、武器伤害、食物使用、
> 商店做汤/收取、F3 角色坐标、背包面板、荆棘死亡/床边复活、
> 死亡菜单（复活/读自动存档/主菜单）、叙事/AI/世界对象/生命存档、
> 林地敌人反击与受击闪烁/伤害数字，数据/配置/地图 JSON/PNG 纹理均已接通；
> 3 作物 / 2 敌人 / 12 物品 / 8 SFX + 1 音乐已由 JSON 提供，玩家与敌人都用
> 3 帧挥砍 + `hit` 帧判定；剩余正式美术/音频素材与敌人专属招式。
> 详见 [计划完成情况](docs/plan-status.md) 与 [垂直切片](docs/roadmap/vertical-slice.md)。

## 目标

- **星露谷式农场循环**：耕地、播种、浇水、生长、收获、季节与天气。
- **实时动作战斗**：近战/远程、敌人 AI、受击、死亡、掉落与装备成长。
- **自研轻量引擎**：SDL3 + OpenGL 4.1 Core，C++20，MSBuild + vcpkg。
- **数据驱动内容**：JSON 定义物品、作物、敌人、对话与配方；C++ 实现系统逻辑。
- **模块化解耦**：严格分层依赖、接口 + 事件通信、可独立测试与替换。

## 技术基线

| 项 | 选择 |
|---|---|
| 语言 | C++20 |
| IDE / 构建 | Visual Studio 2026 + 原生 MSBuild `.sln` / `.vcxproj` |
| 包管理 | vcpkg manifest 模式 |
| 平台 | Windows 10/11 x64（后续再考虑其他平台） |
| 窗口 / 输入 / 音频 | SDL3 |
| 图形 | OpenGL 4.1 Core（自研 42 项 GL 函数表，不引 glad） |
| 数学 | glm |
| 图像解码 | stb_image（app 注册为 `IImageDecoder`）；纹理优先读 `assets/textures/**/*.png`，缺图回落 catalog 色块 |
| 数据格式 | nlohmann/json |
| 调试 UI | M09 即时模式 UI（不引 ImGui） |
| 测试 | Catch2 |
| 逻辑帧率 | 固定 60 Hz，渲染可变帧率 |
| 世界单位 | 1 世界单位 = 1 逻辑像素；默认瓦片 16×16 |

详细说明见[技术栈](docs/architecture/tech-stack.md)。

## 文档入口

- [文档索引](docs/index.md) —— 全部文档的导航与阅读顺序。
- [代码速查](docs/code-reference.md) —— 28 个模块的命名空间、依赖、类型、方法与常量。
- [架构总览](docs/architecture/overview-design.md) —— 分层、主循环、时间、坐标、ECS、线程与存档边界。
- [愿景与设计支柱](docs/architecture/vision-and-pillars.md) —— 产品定位、核心循环与 v1 范围。
- [模块地图](docs/architecture/module-map.md) —— 28 个模块的职责、层级与依赖图。
- [依赖规则](docs/architecture/dependency-rules.md) —— 允许/禁止的引用方式与编译期隔离。
- [技术栈](docs/architecture/tech-stack.md) —— 工具链、第三方库、构建与 CI。
- [编码与内容规范](docs/architecture/conventions.md) —— 命名、目录、错误处理、日志与 JSON 约定。
- [里程碑](docs/roadmap/milestones.md) —— Phase A–F 的交付物与验收标准。
- [垂直切片](docs/roadmap/vertical-slice.md) —— 第一个可玩版本的具体范围。
- [架构决策记录](docs/adr/README.md) —— 已确认的关键决策与背景。

> 说明：`docs/README.md` 与 `docs/architecture/overview.md` 是早期脚手架生成的占位文件，内容已废弃。请以 `docs/index.md` 和 `docs/architecture/overview-design.md` 为准；这两个占位文件可在后续提交中删除。

## 模块分层

| 层 | 职责 | 模块 |
|---|---|---|
| L0 基础设施 | 构建、基础工具、平台、事件/任务、时间循环 | M00 Build、M01 Core、M02 Platform、M03 Events & Jobs、M04 Time |
| L1 引擎服务 | 资源、渲染、输入、音频、UI、动画 | M05 Assets、M06 Rendering、M07 Input、M08 Audio、M09 UI、M10 Animation |
| L2 世界运行时 | 场景/ECS、地图、物理、存档 | M11 Scene/ECS、M12 Tilemap、M13 Physics、M14 Save |
| L3 玩法系统 | 内容库、玩家、交互、物品、种植、战斗、AI、时间天气、NPC、经济、流程 | M15–M25 |
| L4 工具 | 编辑器与调试工具 | M26 Editor、M27 Debug |

完整的模块职责与依赖见[模块地图](docs/architecture/module-map.md)。

## 建议实现顺序

1. **Phase A 能跑能画**：M00 → M01 → M02 → M04 → M05 → M06 → M07。
2. **Phase B 能走能撞**：M11 → M13 → M10 → M12。
3. **Phase C 能种能存**：M15 → M17 → M18 → M19 → M14 → M22（基础）。
4. **Phase D 能打**：M20 → M21 → M09。
5. **Phase E 完整循环**：M25 → M08 → M22（完整）→ M23 → M24。
6. **Phase F 工具化**：M26 → M27。

每个阶段的验收标准见[里程碑](docs/roadmap/milestones.md)。

## 目标仓库结构

```text
Florian-s-Adventure/
├─ src/
│  ├─ engine/        # 引擎层：core / platform / render / input / audio / ecs ...
│  ├─ game/          # 玩法层：player / farming / combat / ai / npc ...
│  └─ app/           # 唯一 composition root 与 main
├─ assets/           # 纹理 PNG（assets/textures/<类别>/，命名见 docs/architecture/conventions.md §11）
├─ data/             # 内容/商店/叙事/音频/资源目录/实体生成表 JSON（格式见 docs/content-format.md）
├─ config/           # game.json（窗口/UI 缩放/路径/玩法参数）
├─ tools/            # 资源导入、校验、打包工具
├─ tests/            # 单元测试与集成测试
├─ docs/             # 架构、模块、路线图、ADR
├─ vcpkg.json
└─ FloriansAdventure.sln
```

## 构建

需要 Visual Studio 2026（Desktop development with C++）与 vcpkg（`x64-windows-static-md`）。

```powershell
$env:VCPKG_ROOT='D:\CPPLibraries\vcpkg'
$msbuild='C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'
& $msbuild FloriansAdventure.sln /p:Configuration=Debug /p:Platform=x64 /m /nologo /v:m
```

三配置：`Debug` / `Development` / `Release` × `x64`；测试在 `build\bin\x64\<Config>\`。
运行时从 `data/`、`config/`、`assets/` 读取数据/配置/图片；删除任意文件都会回落到内置默认值。

## 运行与验证

```powershell
# 冒烟（60 帧、隐藏窗口）
& ".\build\bin\x64\Debug\fa_app.exe" --frames 60

# 可操作种植会话（需要窗口）
& ".\build\bin\x64\Debug\fa_app.exe" --window --play

# 无头自检：种植闭环 + 击杀 dummy + 拾取掉落；失败返回 1
& ".\build\bin\x64\Debug\fa_app.exe" --play-test

# soak：模拟 20 秒（每 10 秒存读档一次），比对实体/纹理计数
& ".\build\bin\x64\Debug\fa_app.exe" --soak 20

# 分辨率覆盖：1920x1080 启动 smoke
& ".\build\bin\x64\Debug\fa_app.exe" --resolution 1920x1080 --frames 5

# 压力基线：1000 精灵 + 20 idle agent，输出 p50/p99 帧时间
& ".\build\bin\x64\Release\fa_app.exe" --stress 1000
```

`--window` 现在直接进入 play 模式（`--play` 仍可显式指定）；`--frames N` 无窗口时
仍跑 scripted smoke。`--play` 操作：

| 输入 | 动作 |
|---|---|
| `WASD` | 移动（八向朝向） |
| `Shift` | 奔跑，持续消耗体力 |
| `Space` | 闪避/冲刺，消耗一次体力 |
| `1`–`9` / 滚轮 | 选择快捷栏（1 锄头、2 浇水壶、3 萝卜种子） |
| 左键 | 使用当前工具/种子；指向成熟作物自动收获（消耗体力） |
| 右键 | 近战攻击（3 帧挥砍，`hit` 帧才判定；伤害读取装备武器 `damage`）；林地史莱姆/蝙蝠会追击/反击；命中显示受击闪烁、`-N` 伤害数字与 SFX，敌人死亡掉落并自动拾取 |
| `F` | 食用选中的食物（汤：+20 HP、+30 体力） |
| `E` | Elder 对话；床边睡觉；出货箱出货；商人开商店（季节库存/每日余量/今日价，可做汤/收取） |
| 林地 | 史莱姆与快速蝙蝠会主动索敌/攻击，出招前有 3 帧挥砍起手；被打倒同样进入死亡菜单 |
| `I` | 打开背包：`ALL/SEED/CROP/FOOD/TOOL/WEAPON/OTHER` 分类 + `PREV/NEXT`/滚轮翻页，点击 MOVE 移动物品 |
| `J` | 打开任务日志（进行中/已完成与目标进度） |
| `Esc` | 关闭面板 / 暂停（继续/保存/存档槽/主菜单） |
| 死亡 | GameOver 面板：`RESPAWN AT BED` / `LOAD AUTOSAVE` / `MAIN MENU`（鼠标） |
| 反引号 | 调试控制台（`time.addDays N`、`time.set HH:MM`、`give`、`teleport`） |
| `F3` / `F4` / `F5` | 统计（含角色世界/格子坐标）/ 日志 / 物理调试（非 Release） |

体力数值在 `config/game.json` 的 `stamina` 段配置：`max`、`regen_per_second`、
`run_drain_per_second`、`dodge_cost`、`attack_cost`、`tool_cost`。
武器伤害与食物效果在 `data/content.json` 的 `damage`、`heal_amount`、
`stamina_restore` 字段配置。商店面板提供 `COOK SOUP`（消耗 2 个萝卜，30 游戏分钟）与 `COLLECT`；
`data/shops.json` 的 `seasons` 控制季节库存，`price_swing` 控制按日价格波动幅度。
`data/audio.json` 配置 8 个 SFX 与 1 首循环音乐（程序化音色），`data/catalog.json`
注册纹理；完整字段见 [内容 JSON 格式](docs/content-format.md)。

存档包含日期/时间/天气、位置/朝向/生命/耐力、背包/快捷栏/装备、农田/作物、
金币/出货/加工/商店余量、地面掉落、敌人存活与生命、杂物/放置物、AI 状态/目标，
以及叙事好感/flags/每日上限/任务进度；读档会重建当前地图后逐段应用。
存档当前为 v2；打开 v1 存档时由 `registerDefaultMigrations()` 自动补齐空段。

## 许可证

TBD。
