# 文档索引

Florian's Adventure 的文档入口。接口以代码与头文件的 Doxygen 注释为准；本文与下列文档描述架构、
约定与现状。

> **要继续开发**：先读 [计划完成情况](status/plan-status.md) —— 每个模块的状态、遗留风险与下一步。
>
> **要查代码**：读 [代码速查](reference/code-reference.md) —— 按模块列出命名空间、依赖、公开头文件、类型、方法与常量。
>
> **要完善程序**：读 [已知限制与后续改进](status/known-limitations.md) —— 可勾选的改进清单。
>
> **度量基线**：虚拟分辨率、瓦片与碰撞盒见 [像素度量](architecture/pixel-metrics.md)（16×16 瓦片、640×360、脚部盒 10×6）。

## 目录结构

```text
docs/
  index.md                    本文件：导航
  guide/                      协作指南（怎么加东西）
    adding-map-components.md  新增地图组件：图层、id 登记、贴图与回落、catalog 双源、区域、验证
  reference/                  开发者参考（怎么查现状）
    code-reference.md         模块符号速查
    content-format.md         content / entities / shops / audio / catalog / maps 的 JSON 契约
  status/                     现状文档
    plan-status.md            模块状态与下一步
    known-limitations.md      已知限制与改进清单
  architecture/               架构基线（为什么这么设计）
```

## 建议阅读顺序

0. [计划完成情况](status/plan-status.md) —— 做到哪了、下一步做什么。
1. [愿景与设计支柱](architecture/vision-and-pillars.md) —— 这个游戏是什么，不做什么。
2. [架构总览](architecture/overview-design.md) —— 分层、主循环、时间、坐标、ECS、线程与存档边界。
3. [模块地图](architecture/module-map.md) —— 模块的职责与依赖图。
4. [依赖规则](architecture/dependency-rules.md) —— 什么能 include 什么。
5. [技术栈](architecture/tech-stack.md) —— 工具链、第三方库、构建与 CI。
6. [编码与内容规范](architecture/conventions.md) —— 命名、目录、错误处理、JSON 约定。
7. [美术资源策略](architecture/art-policy.md) + [新增地图组件指南](guide/adding-map-components.md) —— 素材与组件的工作流。
8. [内容 JSON 格式](reference/content-format.md) —— 数据文件的字段与校验。

## 分类一览

### 协作指南（guide/）

| 文档 | 内容 |
|---|---|
| [新增地图组件](guide/adding-map-components.md) | 瓦片 / 组件 / 区域 / 对象四种形态、ground/decor 图层、瓦片 id 登记、贴图要求与色块回落、双源 catalog、素材生成、放置对象清单、碰撞区与耕地区、验证 |

### 开发者参考（reference/）

| 文档 | 内容 |
|---|---|
| [代码速查](reference/code-reference.md) | 各模块的命名空间、依赖、头文件、类型、方法、枚举与常量 |
| [内容 JSON 格式](reference/content-format.md) | `content` / `entities` / `shops` / `audio` / `catalog` / `maps` 的字段、默认值与校验规则 |

### 现状文档（status/）

| 文档 | 内容 |
|---|---|
| [计划完成情况](status/plan-status.md) | 模块状态、接口面、遗留风险、下一步 |
| [已知限制](status/known-limitations.md) | 各模块设计 / 实测限制汇总成的可勾选清单 |

### 架构基线（architecture/）

| 文档 | 内容 |
|---|---|
| [愿景与设计支柱](architecture/vision-and-pillars.md) | 定位、核心循环、v1 范围、成功标准 |
| [架构总览](architecture/overview-design.md) | 分层、主循环、帧阶段、时间模型、坐标、ECS、资源、场景栈、线程、错误、存档 |
| [模块地图](architecture/module-map.md) | 模块总表、依赖图、层边界 |
| [依赖规则](architecture/dependency-rules.md) | 分层矩阵、同层通信、composition root、include 规则 |
| [技术栈](architecture/tech-stack.md) | C++20、编译选项、vcpkg、资源流水线、测试、CI |
| [编码与内容规范](architecture/conventions.md) | 命名、目录、错误处理、日志、ECS 约定、JSON 规范、Git 提交 |
| [美术资源策略](architecture/art-policy.md) | 色块占位规格、颜色约定、真实美术的接入方式 |
| [像素度量](architecture/pixel-metrics.md) | 虚拟分辨率、瓦片、精灵锚点、碰撞盒、排序与缩放 |

## 维护约定

- 文档与代码同仓演进：改行为同步改文档，走同一个提交。
- 新增模块 / 接口：更新 [模块地图](architecture/module-map.md) 与 [代码速查](reference/code-reference.md)；
  接口文档写在头文件的 Doxygen 注释里，不单独维护设计稿。
- 模块 ID 一旦分配不再复用。
