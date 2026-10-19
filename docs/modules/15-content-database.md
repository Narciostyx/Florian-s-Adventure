# M15 Content Database

| 项 | 值 |
|---|---|
| 层 | L3 |
| 优先级 | P0 |
| 状态 | 已实现 |
| 依赖 | M00–M05（M14 后续接入） |
| 被依赖 | M16–M26 |

## 1. 目的

把可配置内容（物品、作物、配方、敌人、掉落）集中为带 ID 的只读数据库，并在加载时做引用校验。
玩法代码只查表，不硬编码内容。加载器接受 JSON 文本而不是路径：文件访问归 M05，
内容模块因此在没有平台、渲染器或窗口时也能测试。

## 2. 范围

**已实现**

- `ContentId`：命名空间化小写 ID（`item.turnip`、`crop.turnip`），带 FNV-1a 64 哈希。
- 定义类型：`ItemDef`、`CropDef`、`RecipeDef`、`EnemyDef`、`LootTableDef`、`LootEntry`。
- 季节：`Season` 位掩码（Spring/Summer/Fall/Winter/Any），作物与配方共用。
- JSON 加载：`schema_version: 1`，支持 `items`/`crops`/`recipes`/`enemies`/`loot_tables`，
  多次 `loadDocument()` 合并。
- 结构校验：必填字段、类型、正数、堆叠/价格/数量/概率范围、ID 前缀、重复 ID、阶段数组长度。
- 引用校验：作物种子/收获物、配方输入/输出、敌人掉落表、掉落表条目 → 物品。
- 错误收集：一次加载报告全部结构错误与引用错误，`errors()` 可按顺序打印。
- 查询：按 `ContentId`/字符串取强类型定义、`itemsWithTag`、`itemsWithCategory`、全表 span。
- 版本：`schemaVersion()`、`contentVersion()`（每次成功加载递增）、`ContentStats`。
- 失败重试：坏文档不会推进版本；修好后再 `loadDocument()` 即可通过 `validate()`。

**后续接入（本模块不负责）**

- 文件/目录发现：composition root 通过 M05/`IFileSystem` 读文件后传入文本。
- 瓦片定义：M12 的 `tilemap::TileDef` 已存在，避免重复；M15 暂不建第二套。
- 商店/对话/任务/动物行为表：后续扩展。
- JSON Schema：目前是手写校验器，错误信息更可控。
- 本地化：`displayName` 仍是文本，后续可换 `locKey`。
- 资源存在性：只校验 `AssetId` 语法，不校验 catalog 是否声明；需要 M05 的 catalog。
- 存档字段校验：M14 落地后再接。

## 3. 核心概念与数据结构

```cpp
class ContentId {
    std::string m_name; // item.turnip
    std::uint64_t m_hash; // 0 when invalid
};

struct ItemDef {
    ContentId id;
    std::string displayName, description, category;
    /// 可选玩法映射：hoe/watering_can/axe/pickaxe/sword/seed/harvest/placeable。
    std::string toolKind;
    assets::AssetId icon;
    int maxStack = 99, sellPrice = 0;
    std::vector<std::string> tags;
};

struct CropDef {
    ContentId id, seedItem, harvestItem;
    std::vector<int> stageDays;
    std::vector<assets::AssetId> stageSprites;
    std::uint8_t seasons = Season::Any;
    bool regrow = false;
    int regrowDays = 0;
};

struct RecipeDef {
    ContentId id;
    std::vector<ItemAmount> inputs;
    ItemAmount output;
    int requiredMinutes = 0;
    std::string machineKind;
    std::uint8_t seasons = Season::Any;
};

struct EnemyDef { ContentId id; int maxHealth, damage; float moveSpeed; ContentId lootTable; tags; };
struct LootEntry { ContentId item; int minAmount, maxAmount; float chance; };
struct LootTableDef { ContentId id; std::vector<LootEntry> entries; };
```

- ID 规则：非空、至少一个 `.`、只允许 `a-z`/`0-9`/`_`/`.`、不能以点开头/结尾、不能连续点。
- 前缀规则（数据库层强制）：物品 `item.`、作物 `crop.`、配方 `recipe.`、敌人 `enemy.`、掉落 `loot.`。
- 定义是只读值；运行时实体应存 `ContentId`，不要长期存指针——热重载/继续追加会让 `vector` 重分配。
- `RecipeDef` 同时覆盖 ADR-0002 的加工链：`inputs`/`output`/`requiredMinutes`/`machineKind`/`seasons`。
- `extra` 弱类型字段没有实现：高频字段都有强类型，宁可加载时报错也不要运行时猜。

## 4. 公共接口

```cpp
class ContentDatabase {
public:
    core::Status loadDocument(std::string_view json); // parse + merge + validate
    core::Status validate();                         // re-check references
    void clear() noexcept;

    const ItemDef* item(const ContentId&) const noexcept;
    const ItemDef* item(std::string_view) const noexcept;
    // crop / recipe / enemy / lootTable 同理

    std::span<const ItemDef> items() const noexcept;
    std::span<const CropDef> crops() const noexcept;
    std::span<const RecipeDef> recipes() const noexcept;
    std::span<const EnemyDef> enemies() const noexcept;
    std::span<const LootTableDef> lootTables() const noexcept;

    std::vector<const ItemDef*> itemsWithTag(std::string_view) const;
    std::vector<const ItemDef*> itemsWithCategory(std::string_view) const;

    std::span<const ContentError> errors() const noexcept;
    ContentStats stats() const noexcept;
    int schemaVersion() const noexcept;
    std::uint64_t contentVersion() const noexcept;
};
```

- `loadDocument` 先解析结构，再调用 `validate()`；有错误时返回 `kValidationFailed`，
  `errors()` 保留全部结构 + 引用错误。
- 不合法 JSON / 根不是对象 / 缺 `schema_version` / 版本不支持会立即返回对应错误码。
- `loadDocument` 可多次调用做多文件合并；同一类型重复 ID 报 `kDuplicateId`，不覆盖。
- 失败不推进 `contentVersion`；已加载的数据保留，修好后再次加载即可。
- 查询字符串 ID 会先校验格式；格式错误直接返回 `nullptr`，不抛异常。

## 5. JSON 文档格式

```json
{
  "schema_version": 1,
  "items": [
    {"id": "item.turnip_seed", "display_name": "Turnip Seed", "category": "seed",
     "tool_kind": "seed", "max_stack": 99, "sell_price": 5, "tags": ["seed", "spring"]},
    {"id": "item.turnip", "display_name": "Turnip", "category": "crop",
     "max_stack": 99, "sell_price": 35, "tags": ["crop", "spring"]}
  ],
  "crops": [
    {"id": "crop.turnip", "seed_item": "item.turnip_seed", "harvest_item": "item.turnip",
     "stage_days": [1, 2, 2],
     "stage_sprites": ["tex.ground.soil", "tex.ground.grass", "tex.ground.grass"],
     "seasons": ["spring", "fall"], "regrow": false, "regrow_days": 0}
  ],
  "recipes": [
    {"id": "recipe.turnip_soup", "inputs": [{"item": "item.turnip", "amount": 2}],
     "output": {"item": "item.turnip_soup", "amount": 1},
     "required_minutes": 30, "machine_kind": "kitchen", "seasons": "any"}
  ],
  "enemies": [
    {"id": "enemy.slime", "max_health": 30, "damage": 5, "move_speed": 40,
     "loot_table": "loot.slime", "tags": ["slime"]}
  ],
  "loot_tables": [
    {"id": "loot.slime", "entries": [
      {"item": "item.turnip_seed", "min_amount": 1, "max_amount": 2, "chance": 0.5}
    ]}
  ]
}
```

- `seasons` 接受单个字符串或字符串数组；`any` 是 15。
- `stage_sprites` 可选；存在时必须与 `stage_days` 等长。
- `regrow: true` 要求 `regrow_days > 0`。
- 掉落 `chance` 在 0..1；`max_amount >= min_amount`。
- `tool_kind` 可选；只接受 `none/hoe/watering_can/axe/pickaxe/sword/seed/harvest/placeable`。
  M15 只保存字符串，不依赖 M17 的枚举；组装根在使用时转换。
- 缺省值：`max_stack` 99、`sell_price` 0、`seasons` any、`required_minutes` 0、
  `amount` 1、敌人 100/0/60、掉落 1/1/1。

## 6. 依赖关系

- 直接引用 `fa_core`（`Result`/`FNV`）与 `fa_assets`（`AssetId`）。
- **不引用 M11/M12/M16/M17**：内容定义只存 ID 与值，不含实体指针、瓦片结构或玩法系统。
- 文件读取归 M05/composition root；`loadDocument` 只接收文本。
- 存档（M14）负责运行时状态；内容表本身是只读的。
- `ContentDatabase.cpp` 使用 nlohmann/json，因此 `fa_content` 单独打开 `/EHsc`；
  没有代码 throw/catch。

## 7. 实现与测试

```text
src/engine/include/fa/content/   ContentId, ContentTypes, ContentError,
                                 ContentDatabase
src/engine/content/src/          ContentId.cpp, ContentTypes.cpp,
                                 ContentDatabase.cpp
```

测试：`tests/content.test.cpp`，11 个用例 / 178 条断言，覆盖：

- ID 格式/哈希/无效值；
- 季节解析、掩码组合与 `seasonAllows`；
- 完整文档加载、计数、版本、按 ID/标签/类别查询；
- 多文档合并、重复 ID 拒绝且不覆盖旧值；
- 非法 JSON、非对象根、缺 schema、版本不支持；
- 错误 ID、错误前缀、重复 ID；
- 堆叠/价格/数量/概率/阶段数组/regrow 校验；
- 非法季节；
- 四类缺失引用一次收集，补齐后 `validate()` 通过；
- `clear()` 与缺省值。

冒烟：`SmokeSink::initializeContent()` 加载内联内容包；`m15:` 行报告 3 物品、1 作物、
1 配方、1 敌人、1 掉落表、0 错误、schema 1、content v1，并查询 `item.turnip` 的堆叠 99。

## 8. 扩展点

- 新内容类型：新增 `XxxDef` + 解析器 + 引用校验器；查询接口按同样模式扩展。
- 内容包/Mod：多次 `loadDocument` 合并；冲突策略可扩展为“覆盖”或“忽略”。
- JSON Schema：可以加在 `loadDocument` 前做一次快速 schema 检查，手写校验器仍负责跨表引用。
- 本地化：`displayName` 改为 `locKey`，由字符串表解析。
- 资源校验：拿到 `AssetCatalog` 后可校验 `icon`/`stageSprites` 是否存在。
- 强类型 `extra`：需要新字段时优先加真实字段，不要退回弱类型 map。

## 9. 风险与开放问题

- 内容规模增大后全量加载耗时/内存上升；v1 接受，分片/懒加载后续再说。
- 失败加载会保留部分新数据（便于修复重试），不是事务性回滚；多文件发布要小心。
- 指针在继续 `loadDocument`/热重载后可能失效；运行时只存 `ContentId`。
- 不校验 `AssetId` 是否在 catalog 中声明；需要 M05 的 catalog 才能做。
- 没有 JSON Schema/Localization；手写校验器要随类型增长维护。
- 瓦片定义仍归 M12，内容数据库没有第二套 tile 表；若 M26 需要统一编辑，再决定是否迁移。
- `ContentId` 的前缀规则是内容约定；未来跨类型引用（如 `item.*` → `crop.*`）需要更通用的契约。
- 错误码 6000–6008 归 M15；M14 存档继续使用 6009+。

