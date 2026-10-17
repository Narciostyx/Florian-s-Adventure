# M15 Content Database

| 项 | 值 |
|---|---|
| 层 | L3 |
| 优先级 | P0 |
| 状态 | 设计完成 |
| 依赖 | M00–M14 |
| 被依赖 | M16–M26 |

## 1. 目的

把所有可配置内容（物品、作物、瓦片、配方、敌人、掉落、对话、任务、商店）集中为带 ID 的只读数据库，并在加载时做引用校验。玩法代码只查表，不硬编码内容。

## 2. 范围

**负责**

- 内容定义加载：`data/items/*.json`、`data/crops/*.json` 等；支持多文件与目录合并。
- `ContentId`：命名空间化字符串 ID（`item.turnip`、`crop.turnip`），启动时校验唯一性。
- 定义类型：`ItemDef`、`CropDef`、`TileDef`、`RecipeDef`、`EnemyDef`、`LootTableDef`、`ShopDef`、`DialogueDef`、`QuestDef`。
- 引用完整性：外键（掉落表→物品、配方→物品、作物→物品）在加载后校验，失败列出全部错误。
- 查询接口：按 ID 获取强类型定义；按标签/类别过滤。
- 热重载（Development）：重新加载并重建派生表；运行中实体持有 ID 而非指针。
- 内容版本：`contentVersion` 与 schema 校验。

**不负责**

- 运行时实体状态（M11/M19）。
- 存档（M14）。
- 内容编辑 UI（M26）。
- 本地化文本（字符串表可后续接入）。

## 3. 核心概念与数据结构

```cpp
struct ContentId {
    std::string value; // item.turnip
    std::uint64_t hash;
};

struct ItemDef {
    ContentId id;
    std::string displayName;
    std::string description;
    AssetId icon;
    int maxStack = 99;
    int sellPrice = 0;
    std::string category; // seed, crop, tool, weapon, material
    nlohmann::json extra; // 类型特有字段
};

struct CropDef {
    ContentId id;
    ContentId seedItem;
    ContentId harvestItem;
    std::vector<int> stageDays;
    std::vector<AssetId> stageSprites;
    std::string season; // spring/summer/fall/winter/any
    bool regrow = false;
};
```

- 定义是只读的；运行时通过 `const ItemDef*` 使用，热重载时指针可能失效，因此长期存储用 `ContentId`。
- `extra` 字段用于类型特有数据，避免为每个小特性新增类型。

### 数据驱动范围（ADR-0002 决策 2.3）

- 加工链以数据描述：`ProcessingRule { inputId, outputId, requiredMinutes, machineKind, seasonMask }`。
- 季节限制使用 `seasonMask`（位掩码），温室通过 `AreaKind::Greenhouse` 绕过季节判定。
- 动物行为差异以数据表驱动：`AnimalSeasonBehavior { animalKind, season, produceId, intervalDays, requiresIndoor }`。
- 所有 ID 为字符串键，加载期解析为强类型 ID；新增内容不改代码。

## 4. 公共接口草案

```cpp
namespace fa::content {

class IContentDatabase {
public:
    virtual ~IContentDatabase() = default;
    virtual Result<void> loadAll(std::string_view dataRoot) = 0;
    virtual const ItemDef* item(ContentId id) const = 0;
    virtual const CropDef* crop(ContentId id) const = 0;
    virtual const EnemyDef* enemy(ContentId id) const = 0;
    virtual std::vector<const ItemDef*> itemsWithTag(std::string_view tag) const = 0;
    virtual Result<void> reload() = 0;
};

} // namespace fa::content
```

## 5. 依赖关系

- 依赖 M01（Result/哈希）、M05（JSON 资源）、M14（内容相关存档字段校验）。
- 被所有玩法模块依赖；玩法模块不互相依赖内容定义，只依赖 `IContentDatabase` 接口。
- 不依赖 M11；内容定义不包含实体指针。

## 6. 扩展点

- 新增内容类型：新增 `XxxDef` + loader + 校验器，注册到数据库。
- 新增内容包/mod：按目录叠加，ID 冲突策略（覆盖/报错）可配置。
- 新增 schema 校验：JSON Schema 或手写校验器。
- 本地化：`displayName` 改为 `locKey`，由字符串表解析。

## 7. 实现阶段

P0，Phase C。先实现物品/作物/瓦片/配方四类与引用校验；再扩展敌人/掉落/商店/对话/任务。验收：删掉一个被引用的物品会导致启动时明确报错；热重载后新作物可用。

## 8. 风险与开放问题

- 内容规模增大后加载时间与内存上升；v1 全量加载可接受。
- `extra` 字段弱类型，易出错；需要为高频类型提供强类型访问器。
- 热重载与运行中实体的 ID 一致性需严格约束：实体只存 `ContentId`。
- 是否引入 JSON Schema？可显著减少校验代码，但增加工具链依赖。
