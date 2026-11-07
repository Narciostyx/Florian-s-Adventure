# 文档索引

Florian's Adventure 的文档入口。文档体系已于 2026-11-07 精简：**只保留仍在指导开发的必须文档**；
早期的模块设计稿（`modules/`）与逐模块实测记录（`build/`）已完成历史使命——接口以代码与 Doxygen
注释为准，符号索引见[代码速查](reference/code-reference.md)，完整原文保留在 git 历史
（`git show 4280beb:docs/modules/`，提交 `4280beb` 为全量基线）。

> **要继续开发，先读 [计划完成情况](status/plan-status.md)** —— 每个模块的状态、遗留风险与下一步。
>
> **要查代码，读 [代码速查](reference/code-reference.md)** —— 按模块列出命名空间、工程依赖、公开头文件、
> 类型、方法与常量。
>
> **要完善程序，先读 [已知限制与后续改进](status/known-limitations.md)** —— 可勾选的改进清单。
>
> 两条权威基线：产品与范围见 [ADR-0002](adr/0002-v1-scope-decisions.md)，像素与碰撞度量见
> [ADR-0003](adr/0003-tile-and-collision-metrics.md) 与 [像素度量](architecture/pixel-metrics.md)。
> 其他文档与 ADR 冲突时，以 ADR 为准。

## 目录结构

```text
docs/
  index.md                    本文件：导航
  guide/                      协作指南（怎么加东西）
    adding-map-components.md  新增地图组件：图层架构、id 登记、贴图要求、wan 生成流水线、验证
  reference/                  开发者参考（怎么查现状）
    code-reference.md         28 模块符号速查
    content-format.md         content/entities/shops/audio/catalog JSON 契约
  status/                     活文档（现在什么状态）
    plan-status.md            计划完成情况与下一步
    known-limitations.md      已知限制与改进清单
  architecture/               架构基线（为什么这么设计，8 篇）
  adr/                        架构决策记录（Accepted 后不改，推翻用新 ADR）
  roadmap/                    路线图（只保留进行中的垂直切片）
```

## 建议阅读顺序

0. [计划完成情况](status/plan-status.md) —— 做到哪了、下一步做什么。
1. [愿景与设计支柱](architecture/vision-and-pillars.md) —— 这个游戏是什么，不做什么。
2. [架构总览](architecture/overview-design.md) —— 分层、主循环、时间、坐标、ECS、线程与存档边界。
3. [模块地图](architecture/module-map.md) —— 28 个模块的职责与依赖图。
4. [依赖规则](architecture/dependency-rules.md) —— 什么能 include 什么。
5. [技术栈](architecture/tech-stack.md) —— 工具链、第三方库、构建与 CI。
6. [编码与内容规范](architecture/conventions.md) —— 命名、目录、错误处理、JSON 约定。
7. [美术资源策略](architecture/art-policy.md) + [新增地图组件指南](guide/adding-map-components.md) —— 素材与组件的工作流。
8. [垂直切片](roadmap/vertical-slice.md) —— 第一个可玩版本的范围与验收。
9. [架构决策记录](adr/README.md) —— 决策背后的权衡。

## 分类一览

### 协作指南（guide/）

| 文档 | 内容 |
|---|---|
| [新增地图组件](guide/adding-map-components.md) | 花/树/石这类新组件全流程：ground/decor 图层架构、瓦片 id 登记、贴图要求与回落、双源 catalog、wan 生成流水线、放置对象清单、协作规则 |

### 开发者参考（reference/）

| 文档 | 内容 |
|---|---|
| [代码速查](reference/code-reference.md) | 28 个模块的命名空间、依赖、头文件、类型、方法、枚举与常量 |
| [内容 JSON 格式](reference/content-format.md) | `content` / `entities` / `shops` / `audio` / `catalog` 的字段、默认值与校验规则 |

### 活文档（status/）

| 文档 | 内容 |
|---|---|
| [计划完成情况](status/plan-status.md) | 模块状态、接口面、遗留风险、下一步 |
| [已知限制](status/known-limitations.md) | 各模块设计/实测限制汇总成的可勾选清单 |

### 架构基线（architecture/）

| 文档 | 内容 |
|---|---|
| [愿景与设计支柱](architecture/vision-and-pillars.md) | 定位、核心循环、v1 范围、成功标准 |
| [架构总览](architecture/overview-design.md) | 分层、主循环、帧阶段、时间模型、坐标、ECS、资源、场景栈、线程、错误、存档 |
| [模块地图](architecture/module-map.md) | 28 模块总表、依赖图、层边界 |
| [依赖规则](architecture/dependency-rules.md) | 分层矩阵、同层通信、composition root、include 规则 |
| [技术栈](architecture/tech-stack.md) | C++20、编译选项、vcpkg、资源流水线、测试、CI |
| [编码与内容规范](architecture/conventions.md) | 命名、目录、错误处理、日志、ECS 约定、JSON 规范、Git 提交 |
| [美术资源策略](architecture/art-policy.md) | 色块占位规格、颜色约定、真实美术的接入方式 |
| [像素度量](architecture/pixel-metrics.md) | 虚拟分辨率、瓦片、精灵锚点、碰撞盒、排序与缩放 |

### 架构决策记录（adr/）

| 编号 | 标题 | 状态 |
|---|---|---|
| [ADR-0001](adr/0001-core-architecture.md) | 核心架构：分层、自研轻量 ECS、数据驱动、原生 MSBuild | Accepted |
| [ADR-0002](adr/0002-v1-scope-decisions.md) | v1 范围决策：耐力与闪避、季节与温室、加工链、多槽存档、键鼠输入 | Accepted |
| [ADR-0003](adr/0003-tile-and-collision-metrics.md) | 瓦片与碰撞度量：16×16 瓦片、640×360、脚部盒 10×6 | Accepted |
| [ADR-0004](adr/0004-core-math-and-result-shape.md) | Core 的值类型形态：`Rect` 独立结构体、`Result<T>` 无异常 | Accepted |
| [ADR-0005](adr/0005-audio-backend.md) | 音频后端：SDL3 流回调 + M08 自研软件混音，不引入 SDL_mixer | Accepted |
| [模板](adr/template.md) | 新 ADR 使用的模板 | — |

### 路线图（roadmap/）

| 文档 | 内容 |
|---|---|
| [垂直切片](roadmap/vertical-slice.md) | 第一个可玩版本的范围、完成定义、验收脚本 |

## 维护约定

- 文档入库：`docs/` 已纳入 git 跟踪，与代码同仓演进；改行为同步改文档，走同一个提交。
- 新增模块/接口：更新 [模块地图](architecture/module-map.md) 与 [代码速查](reference/code-reference.md)；
  接口文档写在头文件的 Doxygen 注释里，不再单独维护设计稿。
- 模块 ID 一旦分配不再复用；ADR 编号不复用，Accepted 的 ADR 不改内容（推翻用新 ADR）。
- 历史资料：模块设计稿与实测记录（M00–M27，共 55 篇）保存在 git 历史，检索：
  `git show 4280beb:docs/modules/`、`git show 4280beb:docs/build/`。
