# 内容 JSON 格式

本作的内容、敌人摆放、商店、叙事、音频与纹理目录全部由 `data/` 下的 JSON 驱动。
改数值或新增同类内容不需要重新编译；缺失字段会回落到内置默认值。

所有文件都带 `"schema_version": 1`。除 `catalog.json` 由 M05 解析外，其余文件由
M15/M21/M23/M24 或组装根校验；非法条目会被跳过或让整份文档加载失败，并在启动日志
打印原因。

## 1. `content.json`（M15 内容库）

```json
{
  "schema_version": 1,
  "items": [],
  "crops": [],
  "recipes": [],
  "enemies": [],
  "loot_tables": []
}
```

### 1.1 `items[]`

| 字段 | 必填 | 类型 | 说明 |
|---|---|---|---|
| `id` | 是 | string | 必须 `item.` 前缀，全局唯一 |
| `display_name` | 是 | string | HUD/面板显示名 |
| `category` | 是 | string | `seed` / `crop` / `food` / `tool` / `weapon` / `material` …；背包分类过滤按它匹配 |
| `max_stack` | 是 | int > 0 | 单格堆叠上限 |
| `sell_price` | 否 | int ≥ 0 | 商店未上架时的默认卖出价 |
| `tool_kind` | 否 | string | `hoe` / `watering_can` / `seed` / `sword` / `axe` / `pickaxe` / `harvest` / `placeable` |
| `damage` | 否 | int ≥ 0 | 作为武器时的近战伤害 |
| `heal_amount` | 否 | int ≥ 0 | 食用恢复生命 |
| `stamina_restore` | 否 | int ≥ 0 | 食用恢复体力 |
| `tags` | 否 | string[] | 自由标签，供内容查询 |

### 1.2 `crops[]`

| 字段 | 必填 | 类型 | 说明 |
|---|---|---|---|
| `id` | 是 | string | `crop.` 前缀 |
| `seed_item` / `harvest_item` | 是 | string | `item.` 前缀，必须已定义 |
| `stage_days` | 是 | int[] | 每阶段天数，全部 > 0；总和即成熟天数 |
| `stage_sprites` | 否 | string[] | 与 `stage_days` 等长；省略时用占位贴图 |
| `seed_sprite` | 否 | string | 刚播种时显示的贴图 |
| `seasons` | 否 | string[] | `spring`/`summer`/`fall`/`winter`/`any`，默认 `any` |
| `regrow` | 否 | bool | 收获后是否继续生长 |
| `regrow_days` | 否 | int | `regrow=true` 时必须 > 0 |

### 1.3 `recipes[]`

| 字段 | 必填 | 类型 | 说明 |
|---|---|---|---|
| `id` | 是 | string | `recipe.` 前缀 |
| `inputs` | 是 | `{item, amount}[]` | 非空；`item` 为 `item.` 前缀 |
| `output` | 是 | `{item, amount}` | 产物 |
| `required_minutes` | 否 | int ≥ 0 | 加工所需游戏分钟 |
| `machine_kind` | 是 | string | `kitchen` / `furnace` …，必须与调用方一致 |
| `seasons` | 否 | string 或 string[] | 配方可用季节 |

### 1.4 `enemies[]`

| 字段 | 必填 | 类型 | 说明 |
|---|---|---|---|
| `id` | 是 | string | `enemy.` 前缀 |
| `max_health` | 否 | int > 0 | 默认 100 |
| `damage` | 否 | int ≥ 0 | 攻击伤害 |
| `move_speed` | 否 | float > 0 | 追击速度 px/s |
| `loot_table` | 否 | string | `loot.` 前缀 |
| `tags` | 否 | string[] | 自由标签 |

> `enemies[]` 描述“敌人是什么”；真正的摆放位置与 AI 参数在 `entities.json`。

### 1.5 `loot_tables[]`

```json
{"id": "loot.bat", "entries": [
  {"item": "item.bat_wing", "min_amount": 1, "max_amount": 2, "chance": 1.0}
]}
```

`chance` 取 `0..1`；`min_amount`/`max_amount` 为整数，掉落时按区间随机。

## 2. `entities.json`（地图摆放）

```json
{"schema_version": 1, "entities": [
  {"id": "elder", "kind": "npc", "map": "farm", "position": {"x": 280, "y": 176},
   "tint": "#ffc74d", "payload": 23, "radius": 32, "dialogue": "dialogue.elder"},
  {"id": "forest_bat", "kind": "enemy", "map": "forest", "position": {"x": 360, "y": 168},
   "health": 18, "loot": "loot.bat",
   "texture": "tex.enemy.bat", "tint": "#c8b4ff",
   "sight_radius": 190, "attack_range": 24, "move_speed": 90,
   "attack_damage": 7, "attack_cooldown": 0.8}
]}
```

| 字段 | 适用 `kind` | 说明 |
|---|---|---|
| `id` | 全部 | 唯一；敌人还会作为存档 role |
| `kind` | 全部 | `npc` / `bed` / `shipping` / `merchant` / `enemy` / `hazard` |
| `map` | 全部 | 属于哪张地图：`farm`、`forest`、`play/farm`…；**留空 = 所有地图**（早期行的兼容语义） |
| `position` | 全部 | 世界像素坐标；也可用 `cell: {x,y}`（瓦片坐标） |
| `texture` | 可选 | `catalog.json` 中的纹理 id；缺省用内置占位 |
| `tint` | 可选 | `#rrggbb` 或 `#rrggbbaa` |
| `payload` / `radius` | 床/箱/商人/NPC | 交互 payload 与交互半径 |
| `dialogue` | npc | 该 NPC 运行的对话图 id（`narrative.json` 的 `dialogues[].id`）；缺省 `dialogue.elder` |
| `health` / `loot` | enemy | 生命与掉落表 |
| `damage` | hazard | 每次触发伤害 |
| `sight_radius` | enemy | 索敌半径 px |
| `attack_range` | enemy | 进入攻击状态的距离 px |
| `move_speed` | enemy | 追击速度 px/s |
| `attack_damage` | enemy | 命中伤害 |
| `attack_cooldown` | enemy | 两次攻击间隔秒 |

NPC 不再硬编码：地图上有没有可对话对象、它说什么对话图，完全由 `kind: "npc"` 的行决定；
没有该行的地图（如 `--play` 的占位世界）按 `E` 会得到「这张地图没有可对话对象」。

**这份文件由地图编辑器写回**：M26 可视化编辑器的「对象」scope 放置/删除这些行，
「保存」同时写地图 JSON 与 `entities.json`，且只重写当前 `map` 的行——编辑农场不会
动到林地的敌人。因此手改后请保持 `map` 字段，否则该行会被当成「所有地图」。

敌人数量不限；第 1/2 个沿用 `dummy1`/`dummy2` 存档 role，第 3 个起使用自身 `id`
作为 role。新增敌人只需加一条 JSON（并在 `content.json` 加 `enemy.*` 与掉落表），
再在 `catalog.json` 登记纹理。

## 3. `shops.json`（M24 商店）

```json
{"schema_version": 1, "shops": [
  {"id": "shop.general", "name": "杂货店",
   "buy_multiplier": 1.0, "sell_multiplier": 0.5,
   "stock": [
     {"item": "item.turnip_seed", "price": 10, "daily_limit": 5, "price_swing": 0.10},
     {"item": "item.turnip_soup", "price": 150, "price_swing": 0.20,
      "seasons": ["spring", "winter"]}
   ]}
]}
```

| 字段 | 必填 | 说明 |
|---|---|---|
| `id` | 是 | `shop.` 前缀 |
| `name` | 否 | 面板标题 |
| `buy_multiplier` / `sell_multiplier` | 否 | 买/卖价格倍率 |
| `stock[].item` | 是 | `item.` 前缀 |
| `stock[].price` | 是 | 基础价，> 0 |
| `stock[].daily_limit` | 否 | `-1` 不限购；否则每日库存 |
| `stock[].seasons` | 否 | 上架季节；不在当前季节显示 `OUT OF SEASON` 且不可购买 |
| `stock[].price_swing` | 否 | `0..1` 的每日波动幅度；价格 = 基础价 × 倍率 × `[1-swing, 1+swing]`，按 `(item, day)` 确定性生成 |

## 4. `audio.json`（程序化音频）

v1 没有解码器，音频是“音色参数 → PCM”生成；同一份 JSON 可在以后替换为真实文件表。

```json
{"schema_version": 1, "clips": [
  {"id": "sfx.hit", "bus": "sfx", "frequency_hz": 140, "duration": 0.07,
   "amplitude": 0.3, "volume": 0.6, "loop": false},
  {"id": "music.day", "bus": "music", "frequency_hz": 196, "duration": 3.0,
   "amplitude": 0.12, "volume": 0.2, "loop": true}
]}
```

| 字段 | 必填 | 说明 |
|---|---|---|
| `id` | 是 | 代码按 id 触发（`sfx.till`、`sfx.water`、`sfx.plant`、`sfx.harvest`、`sfx.pickup`、`sfx.hit`、`sfx.hurt`、`sfx.buy`、`music.day`） |
| `bus` | 否 | `master` / `sfx` / `music` / `ambience`，默认 `sfx` |
| `frequency_hz` | 是 | > 0 |
| `duration` | 是 | > 0，秒 |
| `amplitude` | 否 | 0..1，波形峰值 |
| `volume` | 否 | 0..1，播放音量 |
| `loop` | 否 | 循环播放；第一条 `bus=music` 且 `loop=true` 作为背景音乐 |

## 5. `catalog.json`（M05 纹理目录）

```json
{"assets": [
  {"id": "tex.enemy.bat", "kind": "texture", "path": "textures/characters/bat.png",
   "size": [16, 16], "color": "#4e3e6e", "altColor": "#947cbe"}
]}
```

| 字段 | 必填 | 说明 |
|---|---|---|
| `id` | 是 | 纹理 id；`content`/`entities`/地图都用它引用 |
| `kind` | 是 | `texture` 或 `json` |
| `path` | 否 | 相对 `assets/` 的路径，形如 `textures/<类别>/<名字>.png`（见[规范第 11 节](../architecture/conventions.md#11-资源命名)）；缺省为纯色占位 |
| `size` | 占位时必填 | `[w,h]` |
| `color` / `altColor` / `tile` | 否 | 占位颜色与棋盘格尺寸 |

app 会遍历 catalog 加载全部 `texture` 条目，因此新增作物/敌人贴图只需：
1. 放 PNG 到 `assets/textures/<类别>/`；
2. 在 `catalog.json` 加一条；
3. 在 `content.json` 的作物阶段或 `entities.json` 的 `texture` 中引用。
不需要改 C++ 句柄表。

## 6. 其它数据文件

- `narrative.json`：NPC/对话/任务，格式见
  `modules/23-npc-dialogue-quests.md`。
- `data/maps/*.json`：瓦片布局、图层、传送点，格式见
  `modules/12-tilemap-and-world.md`。
- `config/game.json`：窗口/UI/玩法/体力参数，格式见
  `build/m02-platform.md` 与 `src/app/GameConfig.hpp`。
  `window.fill` 为 `false`（默认）时按整数倍缩放并留黑边；为 `true` 时按精确比例填满窗口
  （nearest 采样，个别源像素宽度为 2 或 3 个窗口像素）。