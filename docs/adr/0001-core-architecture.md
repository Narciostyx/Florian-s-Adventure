# ADR-0001：核心架构（分层 + 自研轻量 ECS + 数据驱动 + 原生 MSBuild）

- 状态：Accepted
- 日期：2026-01-01（待改为实际讨论日期）
- 决策者：项目所有人
- 相关模块：M00、M01、M06、M11、M15
- 取代：无

## 背景

项目从空仓库开始，目标是一款角角度 2D 像素风的“种植养成 + 实时动作战斗”游戏（类似星露谷）。需求与约束如下：

- 单人开发，长期维护，代码量预期中等（万到十万行量级）。
- 需要同时支持“大量小对象”（作物、粒子、掉落物）与“少量复杂对象”（玩家、NPC、敌人）。
- 内容（作物、物品、敌人、地图）需要频繁调参，不能每次都重新编译。
- 技术栈已固定为 VS2026 + GitHub + vcpkg + SDL3 + OpenGL。
- 拒绝引入完整商业引擎（Unity/Unreal/Godot）以获得完全控制权与学习价值。

驱动器：**可预测的帧时间**、**低成本迭代内容**、**模块可以独立重写**。

## 决策

我们决定采用以下四项互相支撑的架构基石：

1. **五层分层架构，单向依赖**
   - L0 基建：M00 Build、M01 Core、M02 Platform、M03 Events/Jobs、M04 Time/Loop
   - L1 表现：M05 Asset、M06 Rendering、M07 Input、M08 Audio、M09 UI、M10 Animation
   - L2 世界：M11 Scene/ECS、M12 Tilemap、M13 Physics、M14 Serialization
   - L3 玩法：M15–M25
   - L4 工具：M26 Editor、M27 Debug
   - 规则：只允许上层依赖下层；同层用窄接口/事件/纯数据契约；引擎不得 include 玩法代码；唯一 composition root 为 `src/app`。

2. **自研轻量 ECS（非 archetype）**
   - Component 为纯数据，无逻辑；System 每帧按显式顺序执行。
   - 采用稀疏集（sparse set）存储，实体为带版本号的 ID，支持延迟销毁。
   - 不做 archetype/SoA 重排，不做自动依赖分析。

3. **数据驱动内容 + C++ 系统逻辑**
   - 物品、作物、敌人、瓦片、地图、对话、配方均为 JSON。
   - 系统行为（战斗公式、成长规则）写在 C++，不嵌入脚本语言（v1 无 Lua）。
   - 内容在加载时做引用完整性校验，错误在启动阶段而不是运行中暴露。

4. **原生 MSBuild + vcpkg manifest**
   - 手写 `.sln`/`.vcxproj`，每个模块一个静态库项目。
   - 依赖通过 `vcpkg.json` manifest 模式管理，triplet 统一为 `x64-windows-static-md`。
   - 不使用 CMake 作为主构建，避免与 VS 调试器/增量构建体验冲突。

## 考虑过的其他方案

| 方案 | 优点 | 缺点 | 结论 |
|---|---|---|---|
| 使用 Unity/Godot | 开箱即用、生态完整 | 失去底层控制、包体大、学习目标偏离 | 否决 |
| 纯面向对象（无 ECS） | 上手快、调试直观 | 大量小对象时缓存差、行为与数据纠缠 | 否决 |
| 完整 archetype ECS（如 EnTT） | 极致性能、成熟 | 引入重依赖、调试困难、学习成本高 | 否决（性能需求不需要） |
| 单一日志式 ECS 库（自行实现 sparse set） | 轻、可控、能看懂 | 需自己写工具与调试 | **采纳** |
| 嵌入 Lua/Wren 做玩法 | 热重载快、内容扩展性强 | 双重语言、调试复杂、性能边界模糊 | 否决（v1 不做） |
| 全部硬编码 C++ 内容 | 类型安全 | 调参必须重编、无法批量生成 | 否决 |
| CMake + VS 生成器 | 跨平台、社区标准 | VS 集成体验不如原生工程、vcpkg 集成多一层 | 否决（当前仅 Windows） |
| 单一巨大可执行工程 | 配置简单 | 无法表达模块边界、编译慢、循环依赖易发生 | 否决 |

## 后果

正面

- 模块边界由工程文件强制，循环依赖在链接期就会暴露。
- 新增内容不改代码；新增系统不改数据。
- 帧时间可预测（固定逻辑步长 + 显式系统顺序）。
- 每个模块可单独测试，可单独重写。

负面

- ECS 自行实现，需要自建实体检查器（M27）否则调试痛苦。
- 模块多导致工程文件多，初期配置成本高。
- 无脚本意味着每次玩法改动都要重编（Development 配置下增量编译可接受）。
- 分层规则靠人工/CI 约束，容易在赶工时被突破。

中性/需持续关注

- sparse set 在实体数量极大（>10 万）时性能不如 archetype；本项目预期远低于此。
- JSON 内容规模增大后需要校验工具与 schema，否则拼写错误成本高。

## 影响的文档

- `docs/architecture/overview-design.md`
- `docs/architecture/module-map.md`
- `docs/architecture/dependency-rules.md`
- `docs/architecture/tech-stack.md`
- `docs/modules/11-scene-world-ecs.md`
- `docs/modules/15-content-database.md`

## 验证方式

- 代码审查：检查是否存在反向 include（如 `fa/engine/...` 引入 `fa/game/...`）。
- CI：每个模块独立编译；开启 `/W4 /WX`。
- 基准场景：1000 精灵 + 1000 实体 + 100 碰撞体稳定 60 FPS（Phase A/B 验收）。
- 启动校验：内容数据库引用完整性检查必须在 CI 中作为测试运行。

## 备注

后续如需热重载或脚本化，应新增 ADR 并明确与现有 C++ 系统逻辑的边界，而不是逐步把逻辑搬进脚本。
