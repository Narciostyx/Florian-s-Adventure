# 编码与内容规范

## 1. 语言与注释

- 代码标识符、注释、提交信息使用**英文**；面向玩家的文本与设计文档使用**中文**（后续接本地化）。
- **源码文件里只允许 Doxygen 注释**（`//!`、`///`、`\file`、`\brief`、`\param`、`\return`、`\note` 等）。
- 唯一例外是命名空间收尾标记 `}  // namespace fa::core`：它是工具锚点而非叙述性注释，全仓库统一保留。
- 遇到的问题、踩坑过程、设计权衡、修复记录**一律写进 `docs/build/` 或模块文档**，不写进源码；
  源码注释只描述接口契约与“为什么这样设计”，不承载临时性的排障叙述。
- 工程文件（`.vcxproj`/`.props`）可以保留**指向文档的简短说明**（例如“为什么这个开关必要”的一句话），
  但排障过程、失败日志与结论一律写进 `docs/build/`。
- 注释解释“为什么”，不解释“是什么”；公共接口必须有简短文档注释。
- 不提交被注释掉的死代码；用 Git 历史保留。

## 2. 命名

| 类别 | 规则 | 示例 |
|---|---|---|
| 类型/类/结构/枚举 | PascalCase | `AssetManager`、`CropState` |
| 函数/方法 | camelCase | `loadTexture`、`fixedUpdate` |
| 局部变量/参数 | camelCase | `frameDelta`、`entityId` |
| 私有成员 | `m_` + camelCase | `m_allocator`、`m_isLoaded` |
| 静态/全局常量 | `k` + PascalCase | `kFixedDelta`、`kMaxEntities` |
| 宏 | `FA_` + UPPER_SNAKE | `FA_ASSERT`、`FA_LOG_INFO` |
| 枚举值 | PascalCase | `RenderLayer::Actors` |
| 命名空间 | 小写 | `fa::render` |
| 文件名 | PascalCase，与主类型一致 | `AssetManager.h` |

- 布尔变量用 `is/has/can/should` 前缀。
- 避免缩写；统一 `Id` 而不是 `ID`/`id` 混用（类型名 `EntityId`，变量 `entityId`）。
- 不使用匈牙利命名法。

## 3. 目录与文件

```text
src/engine/<module>/
├─ include/fa/<module>/   # 公共头
└─ src/                  # 私有实现
src/game/<feature>/
├─ include/fa/game/<feature>/
└─ src/
src/app/                 # main + composition root
tests/                   # 测试
assets/                  # 资源与内容
```

- 一个头文件一个主要类型；辅助小类型可放同一头，但需有明确归属。
- 公共头使用 `#pragma once`；不使用 include guard 宏。
- 实现文件第一个 include 是自己的头，用于验证自包含性。
- 不把实现细节放进公共头；能用前向声明就前向声明。

## 4. 头文件与 Include

- 公共头路径：`#include <fa/core/Result.h>`；私有头：`#include "LocalHelper.h"`。
- include 顺序：自己的头 → 项目内 → 第三方 → 标准库；每组空行分隔。
- 不在头文件中 `using namespace`；不在全局命名空间放类型。
- 第三方头尽量放实现文件；必须在公共头使用时，用前向声明或轻量包装。
- 禁止在头文件中定义可变全局变量；`inline constexpr` 常量可以。

## 5. 格式

- 使用 clang-format；M00 添加 `.clang-format`：

```yaml
BasedOnStyle: LLVM
IndentWidth: 4
ColumnLimit: 120
BreakBeforeBraces: Attach
PointerAlignment: Left
SortIncludes: CaseSensitive
```

- 4 空格缩进，不使用 Tab。
- 左花括号不换行；单行 `if` 也必须带花括号。
- 一行只声明一个变量；避免超过 120 列。
- 使用 `nullptr`，不用 `NULL`/`0` 表示空指针。

## 6. 错误处理

```cpp
template <typename T>
class Result;

Result<TextureHandle> loadTexture(std::string_view path);
```

- 可恢复错误返回 `Result<T>`；禁止用异常跨模块传播。
- 没有返回值但可能失败的操作返回 `fa::core::Status`（即 `Result<Unit>`）。仓库**不提供**
  `Result<void>`，因为 `void` 无法满足 `Result` 的值语义；所有调用点因此写法统一：

```cpp
fa::core::Status writeText(std::string_view path, std::string_view content);

if (auto status = files.writeText(p, text); !status) {
    return status;                     // 直接向上传递
}
```
- 不可恢复错误使用 `FA_ASSERT`（Debug）或 `FA_FATAL`（所有配置）。
- 不使用 `std::exit`/`abort` 处理可恢复错误；只在 fatal handler 中使用。
- 错误信息包含上下文：模块、操作、路径/ID。
- **异常边界**：全局编译开关是 `/EHsc-`（见 [Directory.Build.props](../../Directory.Build.props)），
  因此绝大多数模块**不能**写 `try/catch`。只有确实要包住第三方库的模块才单独打开 `/EHsc`
  （目前为 `fa_platform`、`fa_events`、`fa_input`、`fa_assets`、`fa_tilemap`、`fa_player` 与 `fa_content`，原因分别是文件系统、
  `<chrono>`、键位 JSON、资源 catalog JSON、地图 JSON、输入头间接引入的 `<unordered_map>` 与内容 JSON），
  且这些模块自己也**不抛异常**——捕获只发生在真正调用第三方 API 的那一层，
  转换后立刻以 `Result`/`Status` 返回。新增模块需要异常边界时，
  在它自己的 `.vcxproj` 里开 `/EHsc`，不要改全局开关。
- **`nlohmann/json` 必须配 `/EHsc`**：它间接 include `<chrono>`，其 tzdb 段含真实的 `try/catch`，
  在 `/EHsc-` 下即便只是正常调用 `parse`/`dump` 也会触发 C4530，`/WX` 直接升级成错误。
  凡是 include 该库的工程都要在 `.vcxproj` 里开 `/EHsc`（详见 [M07 坑 #2](../build/m07-input.md)）。
- **只用 SDL3 / OpenGL 本身不需要 `/EHsc`**：`fa_render` 既不 include `nlohmann/json` 也不 include
  任何会拉进 `<chrono>` 的头（GL 入口点全部是运行期解析的函数指针），因此在全局 `/EHsc-` 下正常编译。
  判断标准不是「模块听起来高不高级」，而是**include 图里有没有 `<chrono>`**
  （详见 [M06 交付内容](../build/m06-render.md)）。

## 7. 日志

| 级别 | 用途 |
|---|---|
| Trace | 逐帧/逐实体细节，仅本地调试 |
| Debug | 开发诊断 |
| Info | 启动、场景切换、存档等里程碑 |
| Warn | 可恢复异常、缺失可选资源 |
| Error | 操作失败但可继续 |
| Fatal | 即将终止 |

- 使用 `FA_LOG_INFO(category, message)` 等宏；不在热路径拼接字符串。
- 分类与 `fa::core::LogCategory` 一一对应，增删需同步 `Log.hpp` 与本表：

| 分类 | 归属模块 | 分类 | 归属模块 |
|---|---|---|---|
| `Core` | M01 Core | `Ecs` | M11 |
| `Platform` | M02 | `Tilemap` | M12 |
| `Time` | M04 | `Physics` | M13 |
| `Asset` | M05 | `Save` | M14 |
| `Render` | M06 | `Content` | M15 |
| `Input` | M07 | `Gameplay` | M16–M20、M25 |
| `Audio` | M08 | `Ai` | M21 |
| `Ui` | M09 | `Economy` | M24 |
| `Animation` | M10 | `Editor` | M26、M27 |
| `Events` | M03 | — | — |
- Release 默认 `Info`；可通过命令行/配置文件调整。
- 日志输出到控制台与文件；文件按大小轮转，保留最近 3 个。

## 8. 内存与所有权

- 默认 `std::unique_ptr` 表达独占；`std::shared_ptr` 仅用于确实共享且生命周期不确定的对象（优先资源句柄）。
- 裸指针/引用只表达**非拥有**关系；公共接口注明生命周期。
- 容器优先 `std::vector`、`std::array`、`std::span`；避免 `std::list`。
- 不在每帧分配大量小对象；使用对象池/arena 时必须在模块文档中说明。
- `new/delete` 只出现在底层分配器；业务代码使用 `std::make_unique` 或容器。

## 9. ECS 约定

- Component 是纯数据 `struct`：无虚函数、无自定义构造/析构、无指针成员。
- 跨实体引用只用 `EntityId`；不存 `Entity*` 或 Component 指针。
- 结构性变更通过 `CommandBuffer`；系统内不直接 `world.destroy()`。
- 需要存档的组件标记 `Persistent` 并注册序列化函数。
- 系统命名 `XxxSystem`；一个系统只负责一个明确的阶段职责。
- 系统不持有 `World&` 成员；每帧通过参数接收。

## 10. JSON 内容规范

- UTF-8，无 BOM；不使用注释；键名 `snake_case`。
- 每个内容文件有 `schema_version`；顶层可以是数组或 `{ "items": [...] }`，全项目统一。
- ID 使用稳定的字符串：`item.turnip`、`crop.turnip`、`enemy.slime`；一旦发布不重命名。
- 引用其他内容时使用 ID 字符串，不嵌入对象。
- 数值：金钱/数量用整数；概率用 0–1 浮点；时间用秒或 tick 并注明单位。
- 枚举用字符串（如 `"spring"`），加载时转换为 `enum class`。
- 可选字段有默认值；未知字段忽略并警告；缺失必填字段视为内容错误。
- 所有内容在加载时校验，并提供 `tools/validate_content`。

示例：

```json
{
  "schema_version": 1,
  "id": "crop.turnip",
  "display_name": "Turnip",
  "seed_item": "item.turnip_seed",
  "harvest_item": "item.turnip",
  "growth_days": 4,
  "seasons": ["spring", "autumn"],
  "regrow_days": 0
}
```

## 11. 资源命名

```text
assets/textures/characters/player_idle.png
assets/textures/tiles/farm_soil.png
assets/audio/sfx/hoe_hit.ogg
assets/content/crops/turnip.json
```

- 全小写 `snake_case`；不用空格、中文、版本号。
- 纹理图集：`<category>_atlas.png` + `<category>_atlas.json`。
- 同一资源的 ID 与路径解耦；ID 稳定，路径可迁移。
- 大文件使用 Git LFS；提交前确认 `.gitattributes`。

### 纹理类别目录

`catalog.json` 的 `path` 相对 `assets/`，一律写成 `textures/<类别>/<名字>.png`。类别与
纹理 ID 的命名空间一一对应，文件名不再重复类别前缀（`textures/items/hoe.png`，
不是 `textures/items/item_hoe.png`）：

| ID 前缀 | 类别目录 | 例 |
|---|---|---|
| `tex.ground.*` | `textures/tiles/` | `textures/tiles/grass.png` |
| `tex.actor.*`、`tex.enemy.*` | `textures/characters/` | `textures/characters/player.png` |
| `tex.crop.*` | `textures/crops/` | `textures/crops/turnip_mature.png` |
| `tex.object.*` | `textures/objects/` | `textures/objects/chest.png` |
| `tex.effect.*` | `textures/effects/` | `textures/effects/slash.png` |
| `tex.item.*` | `textures/items/` | `textures/items/watering_can.png` |
| `tex.marker` 等 UI 辅助图 | `textures/ui/` | `textures/ui/marker.png` |

新增 ID 命名空间时先在表里补一行，再落文件；音频沿用 `audio/<类别>/`。

## 12. Git 与提交

- 分支：`feat/<topic>`、`fix/<topic>`、`docs/<topic>`、`refactor/<topic>`。
- 提交信息使用 Conventional Commits：

```text
feat(render): add sprite batch
fix(save): handle missing save directory
docs(architecture): add module map
```

- 一个提交只做一件事；不提交生成物、`vcpkg_installed/`、`.vs/`。
- PR 描述包含：改了什么、为什么、如何测试、截图/GIF（如适用）。
- 不强制线性历史；合并前 rebase 或 squash 由仓库设置决定。

## 13. 测试规范

- Catch2 `TEST_CASE` 名称描述行为：`"Inventory stacks items up to max stack size"`。
- 一个测试只验证一个行为；使用固定种子。
- 不依赖网络、真实时钟、用户目录；使用临时目录与注入的 `IFileSystem`。
- 渲染逻辑测试“绘制项列表”，不创建 GL 上下文。
- 每个模块至少有一个正向与一个失败路径测试；核心玩法模块需覆盖边界条件。

## 14. 文档规范

- 模块文档使用 `docs/modules/xx-kebab-case.md`，开头标注层、优先级、状态、依赖。
- 新增/修改模块时同步更新[模块地图](../architecture/module-map.md)与[文档索引](../index.md)。
- 架构决策变更写 ADR；不直接改写历史 ADR 的结论。
- 链接使用相对路径；合并前检查死链。
- 图表使用 mermaid；不提交二进制图（截图除外）。

## 15. 代码审查清单

- [ ] 命名、格式、include 顺序符合规范。
- [ ] 没有跨层/反向依赖。
- [ ] 错误路径有 `Result`/日志，没有静默失败。
- [ ] 结构性 ECS 变更走 `CommandBuffer`。
- [ ] 新增内容有 JSON 校验与测试。
- [ ] 公共接口有生命周期与线程约束说明。
- [ ] 没有提交秘密、生成物、大文件（除非 LFS）。
