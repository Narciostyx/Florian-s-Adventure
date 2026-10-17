# M24 Economy / Crafting / Shops

| 项 | 值 |
|---|---|
| 层 | L3 |
| 优先级 | P2 |
| 状态 | 设计完成 |
| 依赖 | M00–M23 |
| 被依赖 | M25、M26 |

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

## 4. 公共接口草案

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

- 依赖 M03（事件）、M11（设备实体）、M15（价格/配方/商店定义）、M18（背包/金钱）、M22（每日刷新/结算）。
- 不依赖 M23；任务奖励通过 `IEconomyService` 或直接物品给予。
- 不依赖 M09；UI 调用服务并展示结果。

## 6. 扩展点

- 新增价格模型（供需、节日溢价）作为 `IPriceModel`。
- 新增加工站类型与多输入/多输出配方。
- 新增拍卖/订单板等交易形式。
- 新增经济统计与成就挂钩。

## 7. 实现阶段

P2，Phase E。先实现钱包、商店买卖、出货箱；再实现配方与加工设备。验收：买卖原子性正确；出货箱次日到账；加工完成后可取回产物。

## 8. 风险与开放问题

- 经济数值平衡需要大量迭代，架构只提供机制。
- 交易原子性在背包满时容易出错；需先检查容量再扣钱。
- 出货箱结算与存档时机的顺序必须确定。
- 是否需要加工/商店进入首个垂直切片？可延后。
