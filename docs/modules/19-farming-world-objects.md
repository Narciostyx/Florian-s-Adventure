# M19 Farming & World Objects

| 项 | 值 |
|---|---|
| 层 | L3 |
| 优先级 | P1 |
| 状态 | 设计完成 |
| 依赖 | M00–M18 |
| 被依赖 | M22、M24、M25、M26、M27 |

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

## 4. 公共接口草案

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

- 依赖 M03（事件）、M11（ECS）、M12（瓦片/覆盖层）、M15（作物/物品定义）、M18（收获物入包）。
- 不依赖 M20/M21；战斗掉落物由 M20 生成，M19 不感知。
- 季节与天气通过 `DayChangedEvent` 传入，M19 不直接读 M22。

## 6. 扩展点

- 新增作物行为（藤蔓、需支架、多次收获）作为 `CropBehavior` 策略。
- 新增土壤类型（温室、沙地）作为耕地属性。
- 新增设备（洒水器、稻草人）实现 `IWorldObjectBehavior`。
- 新增品质系统：`CropInstance` 增加品质字段与概率表。

## 7. 实现阶段

P1，Phase C。先实现耕地/浇水/播种/生长/收获；再实现季节、再生作物、世界对象。验收：萝卜 4 天成熟、浇水加速正确、换季枯萎符合规则、收获进入背包。

## 8. 风险与开放问题

- 按格存储与 ECS 并存，需明确哪类对象用哪种表示。
- 每日结算的原子性：中途崩溃可能导致状态不一致；存档只在结算后写。
- 季节/温室/动物等深度未定，影响数据模型扩展。
- 作物数量大时的渲染批次与剔除需要优化。
