# M19 Farming & World Objects

| 项 | 值 |
|---|---|
| 层 | L3 |
| 优先级 | P1 |
| 状态 | 已实现 |
| 依赖 | M00–M18（实际链接 `fa_core`、`fa_content`、`fa_inventory`、`fa_interaction`、`fa_scene`、`fa_tilemap`） |
| 被依赖 | M22、M24、M25、M26、M27 |

> 实测与踩坑：[构建记录](../build/m19-farming-world-objects.md)

## 1. 目的

实现种植循环与世界对象：耕地、浇水、播种、生长、收获、枯萎，以及箱子、洒水器、加工设备等可放置对象。

## 2. 范围

**负责**

- 耕地状态：`Tilled` 标记、浇水状态、每日干燥。
- 作物实例：`CropInstance`（作物 ID、生长天数、阶段、是否浇水、是否可收获）。
- 生长推进：按天结算（M22 触发 `DayChanged`），而非按秒。
- 季节规则：作物只在允许季节生长；换季枯萎/保留策略可配置。
- 收获：产量、品质（预留）、掉落物生成、是否再生。
- 世界对象：箱子、洒水器、熔炉、工作台、告示牌；占用格与交互。
- 杂物：杂草、石头、树枝；清理产出。
- 工具效果实现：锄地、浇水、施肥、收获、砍伐、采矿。
- 作物渲染：阶段精灵 + 浇水/可收获指示。

**不负责**

- 日历/天气（M22 提供事件）。
- 物品定义与背包（M15/M18）。
- 交互目标选择（M17）。
- 经济/加工配方（M24）。

## 3. 核心概念与数据结构

```cpp
struct Farmland {
    bool tilled = false;
    bool watered = false;
    int daysSinceWatered = 0;
};

struct CropInstance {
    ContentId crop;
    int growthDays = 0;
    int stage = 0;
    bool wateredToday = false;
    bool harvestable = false;
    bool dead = false;
};

struct WorldObject {
    ContentId def;
    glm::ivec2 cell;
    int rotation = 0;
    std::optional<Inventory> storage; // 箱子等
};
```

- 耕地与作物按格子存储在地图覆盖层，不创建实体（作物数量可能很大）。
- 需要每帧更新的对象（洒水器动画）才创建实体。
- 每日结算顺序：浇水判定 → 生长 → 干燥 → 季节检查 → 刷新可收获状态。

### 作物、动物与加工（ADR-0002 决策 2）

- 作物生长单位为**游戏分钟**（不是帧）：`growthMinutes` 累计跨天。
- 季节限制：`CropDef.seasonMask` + `AreaKind::Greenhouse` 绕过；温室作物全年可种。
- 动物：产出按 `intervalDays` 与季节行为表推进，冬季户外不产出。
- 加工链：`ProcessingJob { machineKind, inputId, outputId, remainingMinutes }`，跨天累计，星露谷式语义。
- 所有数值与规则来自 M15 内容库，本模块不含硬编码作物名。

## 4. 公共接口

实际落地是具体类 `fa::farming::FarmingService`（没有引入 `IFarmingService`
接口），外加 M17 适配器 `FarmingToolHandler`。主要方法：

```cpp
namespace fa::farming {

class FarmingService {
public:
    explicit FarmingService(const content::ContentDatabase* content = nullptr);
    core::Status till(tilemap::TileCoord cell, const tilemap::Tilemap& map);
    core::Status water(tilemap::TileCoord cell);
    core::Status plant(tilemap::TileCoord cell, const content::ContentId& seed);
    core::Result<inventory::ItemStack> harvest(tilemap::TileCoord cell,
                                               inventory::Inventory& inventory);
    void onDayChanged(const FarmingDayContext& context);

    const FarmTile* tileAt(tilemap::TileCoord cell) const noexcept;
    const CropState* cropAt(tilemap::TileCoord cell) const noexcept;
    std::span<const FarmTileEntry> entries() const noexcept;
    FarmingStats stats() const noexcept;
    std::span<const FarmingEvent> events() const noexcept;
};

class FarmingToolHandler final : public interaction::IToolHandler {
public:
    bool handles(interaction::ToolKind tool) const noexcept override;
    core::Status use(const interaction::ToolUseContext& context) override;
    /// 组装根在 play 模式把种子扣费/收获入包切换到玩家快捷栏。
    void setInventory(inventory::Inventory& inventory) noexcept;
};

} // namespace fa::farming
```

早期草案（未采用，保留作设计记录）：

```cpp
namespace fa::farming {

class IFarmingService {
public:
    virtual ~IFarmingService() = default;
    virtual Result<void> till(glm::ivec2 cell) = 0;
    virtual Result<void> water(glm::ivec2 cell) = 0;
    virtual Result<void> plant(glm::ivec2 cell, ContentId seed) = 0;
    virtual Result<ItemStack> harvest(glm::ivec2 cell) = 0;
    virtual const CropInstance* cropAt(glm::ivec2 cell) const = 0;
    virtual void onDayChanged(const DayChangedEvent& event) = 0;
};

} // namespace fa::farming
```

## 5. 依赖关系

- 设计上依赖 M03（事件）、M11（ECS）、M12（瓦片/覆盖层）、M15（作物/物品定义）、M18（收获物入包）。
- **实际落地**：`fa_farming` 只链接 `fa_core`、`fa_content`、`fa_inventory`、
  `fa_interaction`、`fa_scene`、`fa_tilemap`。没有直接依赖 M03/M22：每日结算输入是
  `FarmingDayContext`，由组装根从 M22 的 `CalendarEvent` 转换。
- 不依赖 M20/M21；战斗掉落物由 M20 生成，M19 不感知。
- 季节与天气通过 `FarmingDayContext`（`season`/`rain`/`growthMultiplier`）传入，
  M19 不直接读 M22。

## 6. 扩展点

- 新增作物行为（藤蔓、需支架、多次收获）作为 `CropBehavior` 策略。
- 新增土壤类型（温室、沙地）作为耕地属性。
- 新增设备（洒水器、稻草人）实现 `IWorldObjectBehavior`。
- 新增品质系统：`CropInstance` 增加品质字段与概率表。

## 7. 实现落地

P1，Phase C，已完成：

- 耕地/浇水/播种/按天生长/收获；阶段来自 `CropDef::stageDays`。
- 季节检查与温室绕过；换季室外枯萎，温室存活。
- 再生作物收获后回退到 `totalDays - regrowDays`。
- `FarmingToolHandler` 覆盖 Hoe/WateringCan/Seed/Harvest/Axe/Pickaxe；
  M17 新增 `ToolKind::Harvest`。
- `WorldObjectLayer` 与 `DebrisLayer` 提供单格占用/清理与产出。
- app 用萝卜跑通 5 天生长与收获，并分别验证温室存活和室外枯萎；
  摘要 `m19:` 行与 smoke OK 的 `+M19`。

验收：浇水日才生长；雨天自动浇水；生长倍率生效；换季规则正确；收获进入背包。

## 8. 风险与开放问题

- 按格存储与 ECS 并存，需明确哪类对象用哪种表示。
- 每日结算的原子性：中途崩溃可能导致状态不一致；存档只在结算后写。
- 季节/温室/动物等深度未定，影响数据模型扩展。
- 作物数量大时的渲染批次与剔除需要优化。
