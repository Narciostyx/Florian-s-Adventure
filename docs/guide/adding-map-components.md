# 新增地图组件协作指南（瓦片 / 放置对象）

本文面向**后续的人类开发者与 AI 助手**：如何给地图编辑器和游戏世界新增组件（花、树、石头这类），
以及必须遵守的约束。读完本文可以独立完成「一张新贴图 + 一个新组件」的全流程，不需要改引擎代码。

关联基线：[美术资源策略](../architecture/art-policy.md)（色块回落是硬要求）、
[像素度量](../architecture/pixel-metrics.md)（16×16 瓦片）、[编码与内容规范 §11](../architecture/conventions.md#11-资源命名)（命名）、
M12 瓦片地图、M26 可视化编辑器。

## 1. 先选形态：瓦片还是放置对象？

| 形态 | 存在哪 | 编辑器入口 | 适合 | 例子 |
|---|---|---|---|---|
| **瓦片 Tile** | `data/maps/<地图>.json` 的 `tiles` 定义 + 图层格 | 作用域「瓦片」→ 组件面板笔刷、单击查看/双击替换 | 铺满一格的地面/障碍，参与物理与寻路 | grass、wall、soil、water、**flower、tree、stone** |
| **放置对象 Placement** | `data/entities.json`（`EntitySpawn` 表） | 作用域「对象」→ 选种类后点格放置 | 有行为的实体：对话、床、商人、敌人 | npc、bed、merchant、enemy、hazard |

判断口诀：**"站得上去吗 / 挡不挡路吗"→ 瓦片；"有没有交互逻辑"→ 放置对象**。
花、树、石头是纯环境格，选瓦片。新增放置对象见第 9 节。

## 2. 图层架构：地面 vs 装饰（先懂这个再选形态）

每张地图有**两个瓦片图层**（`layers` 数组，按顺序自下而上绘制）：

| 图层名 | render_layer | 放什么 | 贴图要求 |
|---|---|---|---|
| `ground` | `ground` | 地表本身：grass / soil / water / wall / tree 等**整格替换地面**的瓦片 | **满幅不透明**（每格必须完整覆盖，不留透明） |
| `decor` | `ground_detail` | 站在地表之上的物件：flower / stone / 未来的灌木、掉落物、作物装饰 | **透明背景精灵**（只画物件本体，四周留透明） |

为什么这样分（协作时务必遵守）：

1. **季节换色只改草地**：草的春夏秋冬由 `ground` 层的 grass 贴图（或其 tint）决定；
   装饰物件画在 `decor` 层上，换季时**不需要**重新生成"带草的花丛"整图。
2. **可组合扩展**：同一格可以"草地 + 花"、"泥地 + 石头"，后续新增植物直接往 `decor`
   层画，不用为每种地表×物件组合出图。
3. **例外是树**：树冠大、视觉上是"占据整格的地物"，且要参与 solid 阻挡，保留在
   `ground` 层、满幅贴图。判断标准：**会完全盖住地表的单体大物件进 ground，
   小面积站立的装饰进 decor**。

物理与交互：`isSolidCell` 检查**所有**图层——`decor` 层上的石头照样挡路；编辑器
`[`/`]` 切换绘制图层（0=ground，1=decor），F3 面向瓦片面板会显示叠放关系（`under ...`）。

## 3. 瓦片 id 分配表（重要：存档按数字引用！）

| id | name | 图层 | 属性 | 引入批次 |
|---|---|---|---|---|
| 1 | grass | ground | 可走 | 初始 |
| 2 | wall | ground | solid、不可走 | 初始 |
| 3 | soil | ground | tillable | 初始 |
| 4 | bramble | ground | hazard（tint 红） | forest 地图 |
| 5 | water | ground | solid+water（持船时水面可走、陆地反向阻挡） | 水域批次 |
| 6 | flower | **decor** | 可走装饰、透明精灵 | 装饰批次 |
| 7 | tree | ground | solid 障碍、满幅 | 装饰批次 |
| 8 | stone | **decor** | solid 障碍、透明精灵 | 装饰批次 |

规则：
- **新组件取下一个空号，永不复用或改动已有编号**——地图 JSON、存档、编辑器笔刷都按数字引用瓦片。
- 编号上限 65535（`TileId` 为 `uint16`，0 保留给空格）。
- 在本表登记后才能提交（见第 11 节协作规则）。

## 4. 属性旗标语义与互斥

`TileDef`（`src/engine/include/fa/tilemap/TileDef.hpp`）：

| 字段 | 权威 | 说明 |
|---|---|---|
| `walkable` | AI 寻路（M21） | 语义"能站"，不影响物理 |
| `solid` | 物理碰撞（M13） | 真正挡路；与 `walkable` **互斥**（编辑器已强制：勾一个自动清另一个） |
| `tillable` | 农具（M19） | 锄头可开垦 |
| `water` | 玩法豁免标记 | 配合 `Tilemap::setWaterPassable(true)`（乘船）：船上时**只有水可走**，陆地反向变 solid |

例外说明：条件通行（如船过水）**不靠**"walkable+solid 同时开"实现，而是玩法代码对特定
`tag`/旗标做豁免——所以互斥约束只排除矛盾态，不限制条件通行。

## 5. 贴图要求

- 尺寸 **16×16**，PNG，路径 `assets/textures/tiles/<name>.png`（snake_case，文件名不带类别前缀）。
- 风格：16-bit 像素、平涂、统一光向（与现有 grass/soil/water 一致）。
- **ground 层瓦片：满幅不透明**（full-bleed）——底层只有一层地表，透明会露出清屏色；
  走 ground 层的装饰（如 tree）= 本体 + 草地底一体绘制。
- **decor 层瓦片：透明背景精灵**——只画物件本体、四周留透明，落在任何地表之上；
  生成时用洋红底 + 泛洪抠图（见第 8 节 sprite 模式）。

## 6. catalog 登记（双源！两处都要改）

1. `data/catalog.json`（权威）；
2. `src/app/main.cpp` 内嵌 `kCatalogText`（裸 checkout 回落）。

```json
{ "id": "tex.ground.tree", "kind": "texture", "path": "textures/tiles/tree.png",
  "size": [16, 16], "tile": 4, "color": "#2f6a35", "altColor": "#4a8250" }
```

- `path` 相对 `assets/`；`size` 必须与 PNG 实际一致（不一致会有 warn 日志并以图片为准）。
- **`color`/`altColor`/`tile` 必须保留**：缺图时引擎生成双色棋盘回落，冒烟/CI/新克隆不依赖 PNG。
- 忘记同步内嵌副本的后果：`data/` 缺失的环境（裸 checkout 冒烟）里该组件显示为洋红缺失或走
  `textureForName` 兜底——按[美术资源策略 §3](../architecture/art-policy.md) 洋红即"缺东西"信号。

## 7. 地图定义接入

给需要该组件的地图 JSON 的 `tiles` 数组追加一条（未绘制的定义允许存在）：

```json
{ "id": 7, "name": "tree", "texture": "tex.ground.tree",
  "solid": true, "walkable": false, "tillable": false, "water": false,
  "source": [0.0, 0.0, 0.0, 0.0], "tag": "obstacle" }
```

- `name` 就是编辑器组件面板与 F3 面向瓦片面板显示的英文名（ID/日志英文、玩家文案中文）。
- **绘制图层要选对**（第 2 节）：decor 瓦片（flower/stone）只应出现在 `decor` 图层
  （编辑器 `[`/`]` 切层，0=ground 1=decor）；把它们画到 ground 层会因透明背景露出清屏色。
- 当前批次四张地图（`play/farm`、`farm`、`farm-scripted`、`forest`）都登记了 6/7/8，
  并且都带 `decor` 图层（`ground_detail`）；示例只绘在 `play/farm`：花丛 7 格与石头 4 块
  在 decor 层、树 6 棵在 ground 层，其余地图保持空 decor 以免影响脚本烟测断言的格子。
- **不要在 demo 地图（farm-scripted/forest）上随意绘制**：play-test 对出生点、门、荆棘、
  池塘、可耕区有坐标断言。
- 地图校验规则（M12 loader）：id 1..65535 且不重复；每个图层数组长度必须等于
  `width*height`；图层引用的非 0 id 必须已声明（0=空格，无需声明）。

## 8. 用 wan 生成贴图（素材流水线）

模型：`wan2.7-image`（或 pro），API Key 与额度由维护者管理；**被拒绝（配额）就停止并汇报**。

两种提示词模板，按目标图层二选一：

**ground 层（满幅瓦片，不透明）**：

```text
Top-down 2D pixel art game map tile of <主体描述>, sitting on a green grass lawn
background, 16-bit retro pixel art RPG style like Stardew Valley, clean dark outline,
centered, flat colors, uniform lighting, no border, full-bleed, single tile fills the
entire frame, no text
```

**decor 层（透明精灵）**——主体不写草地、背景写死洋红，处理时抠掉：

```text
A <主体描述> game decor sprite, 16-bit retro pixel art RPG style like Stardew Valley,
clean dark outline, centered, isolated on a flat solid pure magenta background
#FF00FF, no shadow, no grass, no ground, no background objects, exactly one <主体>
```

处理（会话工具脚本 `process_wan.py`，或等价步骤）：

- `tile` 模式（ground 层）：裁黑边 → 中心正方形裁剪 → LANCZOS 缩到 16×16。
- `sprite` 模式（decor 层）：边缘泛洪抠除洋红 → 裁剪到内容包围盒 → 等比放入 16×16
  透明画布（水平居中、贴底）。
- QA 必查：与 grass 并排放在草地底色上对比风格/色相/描边；decor 精灵检查透明像素数
  （应占相当比例，全不透明说明抠图失败或提示词没隔离背景）。

## 9. 新增放置对象（entities.json）清单

需要行为逻辑的组件走这条路，涉及代码的地方缺一不可：

1. `data/entities.json`：一行 `{id, kind, map, position|cell, texture, tint, payload, ...}`；
2. `parseEntitySpawns` 的 `knownKind` 白名单加新 kind（否则整行被跳过）；
3. `GameScene::buildWorld` 增加按 kind 生成实体的分支（Sprite/Collider/Interactable 组件）；
4. `editorPlacementKinds()` 加调色板项（kind、中文 label、可选 targets）；
5. `saveEntitySpawns` 的 kind 映射（编辑器写回时保留字段）；
6. 有存档需求的，接入 M25 的 world 段读写；
7. F3 面向瓦片面板想显示它：`facingTileDebugLines` 的 props 表加一项；
8. 编辑器「实体」页签属性行：`buildEditorEntityInfos` 加 kind 分支（键名=JSON 字段名）。

## 10. 验证与提交（准出标准）

```text
MSBuild 三配置（Debug/Development/Release x64）0 error 0 warning
fa_tests            全部用例通过（当前 578 用例 / 30659 断言）
fa_app --frames 15  烟测 OK；m05 无 placeholder 之外的失败；m12 defs 计数符合预期
fa_app --play-test  exit 0、无 "-> bad"
手动：--play → F2 编辑器 → 组件面板出现新瓦片 → 单击看属性 → 双击绘制 → 保存重载仍在
      F3 面向瓦片面板显示新瓦片 name/tag
```

- 若新增了**中文文案**：跑 `tools/build_font_atlas.py` 重烘字体图集（缺字会显示 `?`），
  并确认 `ui_atlas.json` 的 `width/height` 与实际 PNG 一致。
- 提交建议粒度：贴图 PNG → catalog（双源）→ 地图 defs → 文档登记，一次提交一件事。

## 11. 协作规则（人与 AI 共同遵守）

1. **只增不改**：已有瓦片 id/尺寸/旗标是存档与地图的契约；要改先在本文说明影响面。
2. 双源同步义务：第 6 节两处 catalog 必须同一改动内完成。
3. demo 地图的格子是测试资产：`farm-scripted.json`/`forest.json` 的图层数据未经 play-test
   全绿不得改动。
4. 回落优先于报错：任何新组件缺图时必须能以色块运行（catalog 三件套 color/altColor/tile 必填）。
5. 本文的 id 分配表是唯一登记表：新增后立刻更新本文件，避免并行开发撞号。
6. 生成素材被 API 拒绝（额度/鉴权）时：停止生成、保留已完成部分、向维护者汇报，不要重试轰炸。
