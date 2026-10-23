# M24 Economy / Crafting / Shops

| 项 | 值 |
|---|---|
| 层 | L3 |
| 优先级 | P2 |
| 状态 | 已实现 |
| 依赖 | M00–M23（实际链接 `fa_core`、`fa_content`、`fa_inventory`） |
| 被依赖 | M25、M26 |

> 实测与踩坑：[构建记录](../build/m24-economy-crafting-shops.md)

## 1. 目的

提供金钱、商店买卖、加工配方与出货箱，形成“种植/战斗 → 收益 → 升级”的经济闭环。

## 2. 范围

**负责**

- 货币：`Wallet`、收入/支出流水（可选日志）。
- 商店：库存、每日刷新、买卖价格、限购、好感折扣。
- 交易原子性：扣钱与给物品必须同时成功或同时失败。
- 加工配方：输入物品 + 时间/燃料 → 输出物品；设备（熔炉、酿造桶）。
- 出货箱：放入物品，次日结算金钱。
- 价格修正：品质、季节、供需（可选）。
- 事件：`ItemSold`、`ItemBought`、`CraftCompleted`。

**不负责**

- 物品定义（M15）。
- 背包实现（M18）。
- 商店 UI 布局（M09）。
- 任务奖励（M23）。

### 加工链与季节经济（ADR-0002 决策 2.3）

- 加工：`ProcessingJob` 以游戏分钟推进，与 M19 共用推进器语义。
- 加工产物价格随季节波动（`priceSeasonMask`），温室产物不等于当季溢价。
- 商店库存按季节表刷新，`SeasonChanged` 时重建。
- 所有配方与价格在 M15 内容库，本模块只执行规则。

## 3. 核心概念与数据结构

```cpp
struct ShopDef {
    ContentId id;
    std::vector<ShopEntry> stock;
    float buyMultiplier = 1.0f;
    float sellMultiplier = 0.5f;
};

struct ShopEntry {
    ContentId item;
    int price;
    int dailyLimit = -1;
    int remaining = -1;
};

struct CraftingRecipe {
    ContentId id;
    std::vector<ItemStack> inputs;
    ItemStack output;
    float craftSeconds = 0.0f;
    ContentId requiredStation;
};

struct ShippingBin {
    std::vector<ItemStack> pending;
};
```

- 价格以“基础价 × 修正”计算；交易前计算并展示，成交时重算一次以防不一致。
- 加工设备持有 `CraftJob`（配方、剩余时间、输入已扣除）。
- 出货箱在 `DayChanged` 时结算。

## 4. 公共接口

实际落地是 `ShopDatabase` + `EconomyService`，没有引入 `IEconomyService` 接口：

```cpp
namespace fa::economy {

class EconomyService {
public:
    core::Status buy(const content::ContentId& shop, const content::ContentId& item,
                     int count, inventory::Inventory& inventory);
    core::Status sell(const content::ContentId& shop, const content::ContentId& item,
                      int count, inventory::Inventory& inventory);
    core::Status startCraft(const content::ContentId& recipe,
                            const std::string& stationKind,
                            inventory::Inventory& inventory);
    void update(int gameMinutes);
    core::Status collectCraft(std::size_t index, inventory::Inventory& inventory);
    core::Status ship(const content::ContentId& item, int count,
                      inventory::Inventory& inventory);
    int onDayChanged();
    int gold() const noexcept;
};

} // namespace fa::economy
```

商店定义由 M24 本地的 `ShopDatabase` 解析；配方来自 M15 `RecipeDef`。
早期草案（未采用，保留作设计记录）：

```cpp
namespace fa::economy {

class IEconomyService {
public:
    virtual ~IEconomyService() = default;
    virtual Result<void> buy(ContentId shop, ContentId item, int count, inventory::Inventory& inv) = 0;
    virtual Result<void> sell(ContentId item, int count, inventory::Inventory& inv) = 0;
    virtual Result<void> craft(ContentId recipe, inventory::Inventory& inv, scene::EntityId station) = 0;
    virtual void ship(ItemStack stack) = 0;
    virtual int gold() const = 0;
};

} // namespace fa::economy
```

## 5. 依赖关系

- 设计上依赖 M03（事件）、M11（设备实体）、M15（价格/配方/商店定义）、M18（背包/金钱）、M22（每日刷新/结算）。
- **实际落地**：`fa_economy` 链接 `fa_core`、`fa_content`、`fa_inventory`。
  配方来自 M15；商店是 M24 本地 `ShopDatabase`；交易直接操作 M18 `Inventory`。
  v1 由服务持有 `int m_gold`，尚未接 `player::Wallet`；日终由 M25 `endDay()` 调用
  `onDayChanged()`（M22 事件尚未自动路由）。
- 不依赖 M23；任务奖励通过事件/物品给予，金币奖励仍未走 M24。
- 不依赖 M09；UI 调用服务并展示结果。

## 6. 扩展点

- 新增价格模型（供需、节日溢价）作为 `IPriceModel`。
- 新增加工站类型与多输入/多输出配方。
- 新增拍卖/订单板等交易形式。
- 新增经济统计与成就挂钩。

## 7. 实现落地

P2，Phase E，已完成 v1：

- 钱包（`EconomyService::gold()`）、商店买卖、每日限购与重置。
- M15 配方驱动的加工任务：扣输入、按游戏分钟推进、ready 后取回产物。
- 出货箱 pending 与 `onDayChanged()` 结算。
- 交易原子性：先检查金币/背包/库存，再修改状态；加工输入失败会回滚。
- 事件：买入/卖出/加工开始/ready/取回/出货/结算/每日刷新。
- 存档：`shipping()` 导出待结算清单，`restoreShipping()`/`restoreJobs()`/
  `restoreRemaining()` 从 `SaveArchive` 恢复；金币用 `setGold()`。
- app：买种子 → 卖萝卜 → 做汤 → 出货，HUD 显示金币，摘要 `m24:` 与 `+M24`；
  play 模式有橙色出货箱（`E` 全部作物入箱，次日结算）与紫色商人（`E` 打开
  商店面板：买种子/卖萝卜、`COOK SOUP` 做汤、`COLLECT` 收取，`Esc` 关闭）；
  加工按游戏分钟推进，经济段随存档写入并读回。

验收：买卖原子性正确；出货箱次日到账；加工完成后可取回产物；读档后金币、
出货清单与加工任务一致。

## 8. 风险与开放问题

- 经济数值平衡需要大量迭代，架构只提供机制。
- 交易原子性在背包满时容易出错；需先检查容量再扣钱。
- 出货箱结算与存档时机的顺序必须确定。
- 商店面板仍是基础版，没有季节库存、分类与交易确认；季节库存与价格波动待补。
