# M22 Calendar / Weather / Lighting

| 项 | 值 |
|---|---|
| 层 | L3 |
| 优先级 | P1 |
| 状态 | 设计完成 |
| 依赖 | M00–M21 |
| 被依赖 | M19、M23、M24、M25、M26 |

## 1. 目的

提供游戏内时间体系（日/季/年/时刻）、天气状态与昼夜光照表现，并通过事件驱动玩法结算。

### 日历权威定义（ADR-0002 决策 2、3）

- 一天 = 24 游戏小时；v1 一季 = 28 天；四季循环。
- 游戏时间推进节奏：1 游戏分钟 = 0.7 真实秒（可在配置中调整）。
- 自动存档锚点：游戏时间每 12 小时（即 00:00 与 12:00 或可配置偏移）。
- 季节切换事件 `SeasonChanged` 由本模块发出，M19/M21/M23/M24 订阅。
- 本模块不直接操作存档，只提供时间与事件。

## 2. 范围

**负责**

- 日历：`GameDate { year, season, day }`、季节长度（默认 28 天）、一天时长（默认 20 分钟）。
- 时刻：`timeOfDay`（0–1 或 6:00–26:00），日出/日落曲线。
- 时间推进：可配置“时间流速”，睡眠/跳过时间。
- 天气：晴、雨、风暴、雪；按季节权重随机；天气预报（明日）。
- 天气效果：雨水自动浇水、作物生长修正、音效/粒子切换。
- 光照：环境光颜色/强度随时间与天气变化；点光源（灯、火把）叠加。
- 事件：`HourChanged`、`DayChanged`、`SeasonChanged`、`WeatherChanged`。
- 光照渲染：全屏乘法混合的暗化层 + 点光源遮罩（M06 绘制）。

**不负责**

- 作物生长规则（M19 消费 `DayChanged`）。
- NPC 日程（M23 可消费 `HourChanged`）。
- 存档字段（M14 保存日期/天气）。
- 高级光照（法线、阴影投射）。

## 3. 核心概念与数据结构

```cpp
enum class Season { Spring, Summer, Fall, Winter };
enum class Weather { Sunny, Rainy, Stormy, Snowy };

struct GameDate {
    int year = 1;
    Season season = Season::Spring;
    int day = 1;
};

struct WorldClock {
    GameDate date;
    float timeOfDay = 6.0f; // 6..26
    float dayLengthMinutes = 20.0f;
    bool paused = false;
};
```

- 一天 6:00 开始；26:00 视为次日 2:00，强制睡眠结算。
- 天气在每天开始时确定；`WeatherChanged` 在 `DayChanged` 之前发布。
- 光照只影响表现，不影响玩法判定（除特定怪物生成，可后续）。

## 4. 公共接口草案

```cpp
namespace fa::world {

class IWorldClock {
public:
    virtual ~IWorldClock() = default;
    virtual void update(const time::FrameContext& time) = 0;
    virtual const WorldClock& clock() const = 0;
    virtual void skipToNextDay() = 0;
    virtual Weather today() const = 0;
    virtual Weather tomorrow() const = 0;
    virtual glm::vec3 ambientColor() const = 0;
};

} // namespace fa::world
```

## 5. 依赖关系

- 依赖 M03（事件）、M04（时间）、M06（光照绘制）、M11（光照实体/系统）、M15（天气/季节配置）。
- 不依赖 M19；通过事件让 M19 自行处理生长与浇水。
- M25 负责睡眠/结算流程；M22 提供 `skipToNextDay`。

## 6. 扩展点

- 新增天气类型与效果表（数据驱动）。
- 新增季节事件（节日、集市）。
- 新增光照模型（彩色点光、闪烁、遮罩贴图）。
- 新增时间流速道具/区域（室内时间暂停）。

## 7. 实现阶段

P1，Phase C/E。先实现日期/时刻/事件；再实现天气与光照。验收：20 分钟一天、28 天一季；雨天自动浇水；夜晚变暗且灯光可见。

## 8. 风险与开放问题

- 时间流速与玩家活动节奏需要平衡，影响整体体验。
- 光照层与像素风颜色的兼容性需要美术调色。
- 天气随机性与存档一致性（读档后天气不变）。
- 季节长度、一天时长、睡眠惩罚均为产品决策。
