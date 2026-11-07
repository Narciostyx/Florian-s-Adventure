# ADR-0002：v1 范围决策（战斗、季节、存档、输入）

- 状态：Accepted
- 日期：2026-10-17
- 决策者：项目所有人
- 相关模块：M07、M09、M14、M15、M16、M19、M20、M21、M22、M23、M24、M25、M27
- 取代：无

## 背景

架构文档留下 5 个待确认的产品问题（见 `docs/architecture/overview-design.md` 第 16 节）。本轮确认其中 4 个，瓦片尺寸与碰撞盒比例仍然待定（另见 `docs/architecture/pixel-metrics.md`）。

这些决策直接影响 M20、M19、M22、M24、M14、M07 的数据模型与实现顺序，因此以 ADR 固化，避免后续反复。

---

## 决策 1：战斗包含耐力与闪避

我们决定在 v1 的战斗中同时提供**耐力资源**与**闪避（翻滚）**，两者互相耦合。

### 具体规则（数值均为 JSON 可调）

| 项 | 默认值 | 说明 |
|---|---|---|
| 耐力上限 | 100 | `Stamina.max`，可被装备/状态修改 |
| 攻击消耗 | 15 | 轻击；重击 25 |
| 闪避消耗 | 25 | 不足时不可闪避，播放失败反馈 |
| 回复延迟 | 0.6 s | 消耗后多久开始回复 |
| 回复速率 | 22 /s | 线性回复，可在 UI 显示 |
| 闪避时长 | 0.25 s | 固定，期间不可转向 |
| 闪避位移 | 48 px | 按 ease-out 曲线，忽略输入加速度 |
| 无敌窗口 | 0.05–0.20 s | 翻滚中段无敌，起手与收尾可被击中 |
| 闪避冷却 | 0.5 s | 从闪避结束起算 |
| 打断规则 | 受击硬直可打断收尾，不可打断无敌段 | |

### 设计约束

- 耐力只在逻辑步内结算，不依赖真实时钟。
- 闪避位移走 M13 的移动求解（不直接改位置），防止穿墙。
- 闪避与攻击互斥：攻击后摇内不可闪避（可配置取消窗口）。
- 所有数值放在 `assets/data/combat_tuning.json`，不写死。

### 数据模型

```cpp
struct Stamina {
    float current = 100.0f;
    float max = 100.0f;
    float regenDelayRemaining = 0.0f;
};

struct Dodge {           // 仅在闪避期间存在
    glm::vec2 direction;
    float elapsed = 0.0f;
    float duration = 0.25f;
    float invulnFrom = 0.05f;
    float invulnTo = 0.20f;
};
```

### 影响的模块

| 模块 | 变化 |
|---|---|
| M20 Combat | 新增耐力消耗/回复、闪避判定与无敌窗口、`AttackRequested` 增加耐力校验 |
| M16 Player | 新增 `Dodge` 状态、取消窗口、输入缓冲；耐力不足时的失败反馈 |
| M13 Physics | 闪避位移必须走 `MoveAndSlide`，不绕过碰撞 |
| M10 Animation | 新增 `dodge` clip 与无敌帧标记（动画事件） |
| M09 UI | HUD 耐力条、闪避冷却指示 |
| M21 AI | 敌人可选具备冲锋/后撤（复用位移执行器，不消耗玩家耐力） |
| M27 Debug | 耐力/无敌状态可视化 |

---

## 决策 2：季节限制 + 温室 + 季节化动物 + 星露谷式加工链

### 2.1 季节限制

- 4 季 × 28 天（可配），每季有独立作物白名单。
- 作物定义包含 `seasons: ["spring", "summer"]`；非当前季节不可播种，已种作物换季后**枯萎**（默认），除非位于温室。
- 生长以“天”为单位推进，在日终结算；雨天自动浇水。

### 2.2 温室

- 温室是一种**地图区域属性**，不是独立场景：`FarmArea { areaId, kind: Outdoor | Greenhouse }`。
- 温室内：忽略季节白名单与枯萎规则，始终可生长，夜间不停止生长。
- 温室通过 M12 的区域查询判定，M19 只读 `areaKind`，不硬编码坐标。
- 解锁方式（任务/购买）由 M23/M24 决定，M19 不关心。

### 2.3 动物（季节性行为差异）

- 动物作为世界对象（`Animal` 组件 + 棚舍区域），不是纯 ECS 自由实体。
- 行为表驱动：`AnimalSeasonBehavior` 按季节定义可外出时段、外出概率、产出速率、心情修正。
  - 春季：全天可放牧，产出正常。
  - 夏季：正午高温不外出，需遮阴。
  - 秋季：可放牧，产出略高。
  - 冬季：不外出，只在棚舍活动，产出降低。
  - 雨天：默认不外出（可通过 `weatherOverrides` 覆盖）。
- 心情/好感影响产出品质，与 M23 的好感系统解耦（M19 只暴露事件）。

### 2.4 加工链（模拟星露谷，保证可拓展）

- 设备是世界对象（罐头瓶、酿酒桶、奶酪机…），持有 `ProcessingJob { recipeId, inputStack, startDay, durationMinutes, outputStack }`。
- 加工按**游戏分钟**推进，日终不中断（跨天继续）。
- 配方与设备能力在 M24 的 JSON 中定义；M19 只负责“放置/交互/取出”和计时。
- 可拓展点：`IProcessingRule`（耗时、产出数量、品质判定、是否需要燃料），新增设备不改引擎。

### 数据模型

```cpp
enum class AreaKind { Outdoor, Greenhouse };

struct Crop {
    ContentId seedId;
    int dayPlanted = 0;
    int growthDays = 0;
    bool watered = false;
    bool withered = false;
};

struct Animal {
    ContentId speciesId;
    int friendship = 0;
    int mood = 0;
    bool outdoors = false;
    int lastProduceDay = 0;
};

struct ProcessingJob {
    ContentId recipeId;
    int startGameMinute = 0;
    int durationMinutes = 0;
    ItemStack input;
    bool ready = false;
};
```

### 影响的模块

| 模块 | 变化 |
|---|---|
| M19 Farming | 季节白名单校验、枯萎结算、温室区域、动物、加工设备与计时 |
| M22 Calendar | 季节/天气作为权威数据源；提供 `isRaining()` 与日出日落 |
| M24 Economy | 加工配方、设备定义、产出定价 |
| M15 Content DB | 新增 `CropDef.seasons`、`AnimalDef`、`RecipeDef`、`ProcessingDeviceDef` 及引用校验 |
| M14 Save | 存档需包含作物、动物、加工进度、温室解锁状态 |
| M23 NPC | 温室/动物解锁可作为任务奖励，仅通过事件交互 |
| M12 Tilemap | 新增区域（`FarmArea`）概念与查询接口 |

---

## 决策 3：多槽存档 + 游戏时间每 12 小时自动存档

- 槽位：**3 个手动槽 + 1 个自动存档槽**（`autosave`）。槽位元数据包含玩家名、游戏日期、游玩时长、最后保存时间。
- 自动存档触发：游戏内每 **12 小时**一次（一天 24 小时，默认 06:00 起床 → 12:00 与 24:00/换日各一次）。
- 触发条件：仅当处于**安全状态**（无对话、无战斗、无过场、非加载中）时才执行；否则顺延到下一个安全点。
- 写入流程：序列化到内存 → 写临时文件 → flush → 原子替换 → 保留上一份 `.bak`。失败时记录日志，不中断游戏。
- 自动存档不得阻塞主线程：序列化在主线程（保证一致性快照），磁盘写入可交给工作线程。
- 换日结算后强制存档一次（等同于 24:00 的那次）。

### 槽位索引

```json
{
  "slots": [
    { "id": "slot1", "kind": "manual" },
    { "id": "slot2", "kind": "manual" },
    { "id": "slot3", "kind": "manual" },
    { "id": "autosave", "kind": "auto" }
  ],
  "autosaveIntervalGameHours": 12
}
```

### 影响的模块

| 模块 | 变化 |
|---|---|
| M14 Save | 多槽目录结构、槽位索引、自动存档策略、原子写入与备份 |
| M25 Game Flow | 提供“安全状态”判定；主菜单/读档界面列出槽位；换日强制存档 |
| M09 UI | 存档/读档界面、槽位元数据显示、自动存档提示 |
| M22 Calendar | 提供游戏时间与 12 小时触发点 |
| M27 Debug | 手动触发/禁用自动存档的命令 |

---

## 决策 4：不支持手柄，但保留输入抽象

- v1 只实现 keyboard/mouse 输入设备；**不实现**手柄 provider，不提供手柄 UI 提示，不保证手柄导航。
- 架构上保留 `InputDevice` 抽象与动作映射层：动作（`MoveLeft`、`Attack`、`Dodge`…）与物理按键分离。
- 新增设备只需实现 `InputDevice` 并注册，不改玩法代码。
- 不在公开接口中暴露 SDL 手柄类型（遵守依赖规则）。

### 影响的模块

| 模块 | 变化 |
|---|---|
| M07 Input | 定义 `InputDevice` / `ActionMap` 抽象；仅提供 `KeyboardMouseDevice` |
| M16 Player | 只读动作状态，不关心设备 |
| M09 UI | 提示文案统一使用键鼠；预留 `BindingGlyphProvider` 接口 |
| M01 Core | 无变化 |

---

## 后果

正面

- 战斗有资源管理维度，闪避提供主动防御，深度高于纯站桩输出。
- 季节/温室/动物/加工构成中后期目标，支撑 28 天 × 4 季的节奏。
- 多槽与 12 小时自动存档降低玩家损失，且实现代价低。
- 输入抽象保留，未来接手柄不需重构玩法层。

负面

- 耐力与闪避显著增加 M16/M20 的调参与动画工作量，需要 M27 可视化支持。
- 温室 + 动物 + 加工链使 Phase C 体量变大，垂直切片需要裁剪。
- 自动存档的“安全状态”判定容易遗漏边缘情况（对话中、过场中），需统一由 M25 提供。

需持续关注

- 加工链模拟星露谷容易膨胀为大量设备与配方，v1 建议限制在 6 种设备以内。
- 12 小时自动存档频率对大地图序列化耗时的要求须在 Phase C 实测。

## 影响的文档

- `docs/architecture/overview-design.md`（第 16 节开放问题，其中 4 项已解答）
- `docs/reference/code-reference.md`
- `docs/reference/code-reference.md`
- `docs/reference/code-reference.md`
- `docs/reference/code-reference.md`
- `docs/reference/code-reference.md`
- `docs/reference/code-reference.md`
- `docs/reference/code-reference.md`
- `docs/reference/code-reference.md`
- `docs/roadmap/vertical-slice.md`（切片可按此裁剪）

> 注：上述模块文档的对应章节已按本 ADR 回写完毕（06、07、09、10、12、13、14、15、16、17、19、20、21、22、23、24、25、27）。本 ADR 仍为这些决策的**权威来源**，实现中若发现冲突，以本文件为准并修正模块文档。

## 验证方式

- 手感验收：连续 3 次闪避后耐力不足，HUD 有明确反馈；闪避可穿过投射物但不穿墙。
- 季节验收：春播草莓在夏季枯萎；温室内不枯萎；冬季动物不外出。
- 存档验收：3 个手动槽 + 1 个自动槽互不干扰；自动存档出现在 12:00 与换日；拷走 `.bak` 后可恢复。
- 输入验收：`ActionMap` 重绑定后玩法逻辑无需改动。

## 尚未决定

1. 瓦片尺寸 16×16 或 32×32，以及角色帧与碰撞盒比例（见 `docs/architecture/pixel-metrics.md`）。
2. 耐力/闪避的具体数值手感（需实机调参）。
3. 温室的解锁条件与面积上限。
4. 自动存档是否允许多个历史点（当前设计：仅保留最新 1 份 + `.bak`）。
