# 新增地图组件指南（瓦片 / 组件 / 区域 / 对象）

给地图编辑器和游戏世界新增组件（花、树、石头、家具这类）的流程与约束。按本文可独立完成
「一张新贴图 + 一个新组件」，通常不需要改引擎代码。

关联基线：[美术资源策略](../architecture/art-policy.md)（缺图必须能色块回落）、
[像素度量](../architecture/pixel-metrics.md)（16×16 瓦片）、
[编码与内容规范 §11](../architecture/conventions.md#11-资源命名)（资源命名）。

## 1. 四种形态

| 形态 | 存在哪 | 编辑器入口 | 适合 | 例子 |
|---|---|---|---|---|
| **瓦片 Tile** | 地图 JSON 的 `tiles` 定义 + `ground` 图层格 | 「瓦片」作用域：选地面瓦片，单击查看 / 双击替换 | 铺满一格的地面，参与物理与寻路 | grass、wall、soil、water、bramble |
| **组件 Component** | 瓦片类：`decor` 图层格（`tag` 为 `decor`/`obstacle` 或 `render_height>1` 的定义自动归入）；实体类：`data/entities.json` 的 spawn 行 | 「组件」作用域：选条目后左键放置、右键清除 | 站在地表之上的叠放物与家具；要求下方是特定瓦片（当前=草地），不替换地面 | flower、tree、stone、grass_tuft、床、箱子、荆棘 |
| **区域 Area** | 地图 JSON 的 `portals` / `blockers` / `areas` 数组 | 「区域」作用域：先选新建类型（传送门 / 碰撞区 / 耕地区），再点两格框选 | 矩形地图区域：传送触发、整片禁行 + 立绘贴图、可耕范围 | portal、碰撞区、耕地区域 |
| **对象 Placement** | `data/entities.json`（`EntitySpawn` 表） | 「对象」作用域：选种类后点格放置 | 人物 / 生物 | npc、merchant、enemy |

选择依据：地面本身 → 瓦片；地表之上的物件与家具 → 组件；一片矩形区域 → 区域；会行动的角色 → 对象。

## 2. 图层：ground 与 decor

每张地图有两个瓦片图层（`layers` 数组，自下而上绘制）：

| 图层名 | render_layer | 内容 | 贴图要求 |
|---|---|---|---|
| `ground` | `ground` | 地表本身：grass / soil / water / wall 等整格替换地面的瓦片 | 满幅不透明（不留透明，否则露出清屏色） |
| `decor` | `ground_detail` | 站在地表之上的组件：flower / tree / stone / grass_tuft 等 | 透明背景精灵（只画物件本体） |

要点：

- 季节换色只改 `ground` 层的 grass；`decor` 层物件叠在其上，换季无需重绘整图。
- 同一格可「草地 + 花」「草地 + 树」；新增植物走「组件」作用域画到 `decor` 层，无需为每种地表×物件组合出图。
- `isSolidCell` 检查所有图层，`decor` 层上的石头照样挡路。编辑器用 `[` / `]` 切换绘制图层（0=ground，1=decor）。

**高瓦片（tall tile）**：`TileDef` 有可选 `render_height`（1..16，默认 1）。>1 时提取器把该格渲染成
`16 × 16*N` 的整幅 quad，向上延伸、底边锚定本格，深度排序仍按本格底边。碰撞 / 寻路 / 农事只看到锚定的那一格。
规则：tall def 必须放 `decor` 层；贴图尺寸 `16 × 16*render_height`，与 catalog `size` 同步；树冠会盖住上方格的行走角色（v1 接受）。

## 3. 瓦片 id 分配表

存档、地图 JSON 与编辑器笔刷都按数字引用瓦片，因此 id 是契约。

| id | name | 图层 | 属性 |
|---|---|---|---|
| 1 | grass | ground | 可走 |
| 2 | wall | ground | solid、不可走 |
| 3 | soil | ground | tillable |
| 4 | bramble | ground | hazard（tint 红） |
| 5 | water | ground | solid + water（乘船时水面可走、陆地反向阻挡） |
| 6 | flower | decor | 可走装饰、透明精灵 |
| 7 | tree | decor | solid 障碍、tall（render_height=3，贴图 16×48 透明树冠） |
| 8 | stone | decor | solid 障碍、透明精灵 |
| 9 | grass_tuft | decor | 可通行草丛装饰、透明精灵 |

规则：新组件取下一个空号，不复用或改动已有编号；上限 65535（`TileId` 为 `uint16`，0 保留给空格）；
本表是唯一登记处，新增后同步更新。

## 4. 属性旗标（`TileDef`）

| 字段 | 作用方 | 说明 |
|---|---|---|
| `walkable` | AI 寻路（M21） | 语义「能站」，不影响物理 |
| `solid` | 物理碰撞（M13） | 真正挡路；与 `walkable` 互斥（编辑器强制：勾一个清另一个） |
| `tillable` | 农具（M19） | 锄头可开垦 |
| `water` | 玩法豁免标记 | 配合 `Tilemap::setWaterPassable(true)`（乘船）：船上时只有水可走 |

条件通行（如船过水）靠玩法代码对 `tag`/旗标做豁免实现，不靠 `walkable`+`solid` 同时开；互斥只排除矛盾态。

## 5. 贴图要求

- 尺寸 16×16，PNG，路径 `assets/textures/tiles/<name>.png`（snake_case，文件名不带类别前缀）。
- 风格：16-bit 像素、平涂、统一光向，与现有 grass/soil/water 一致。
- `ground` 层：满幅不透明。`decor` 层：透明背景精灵，生成时洋红底 + 泛洪抠图。高瓦片贴图 `16 × 16*render_height`，底座贴住画布底边。

## 6. catalog 登记（双源）

两处必须同一改动内完成：

1. `data/catalog.json`（权威）；
2. `src/app/main.cpp` 内嵌 `kCatalogText`（缺 `data/` 时的回落）。

```json
{ "id": "tex.ground.tree", "kind": "texture", "path": "textures/tiles/tree.png",
  "size": [16, 48], "tile": 4, "color": "#2f6a35", "altColor": "#4a8250" }
```

- `path` 相对 `assets/`；`size` 必须与 PNG 实际一致（不一致会 warn 并以图片为准）。
- `color` / `altColor` / `tile` 必填：缺图时引擎生成双色棋盘回落，冒烟 / CI / 新克隆不依赖 PNG。

## 7. 地图定义接入

给需要该组件的地图 JSON 的 `tiles` 数组追加一条（未被绘制的定义允许存在）：

```json
{ "id": 7, "name": "tree", "texture": "tex.ground.tree",
  "solid": true, "walkable": false, "tillable": false, "water": false,
  "source": [0.0, 0.0, 0.0, 0.0], "tag": "obstacle" }
```

- `name` 是编辑器组件面板与 F3 面向瓦片面板显示的英文名（ID / 日志英文，玩家文案中文）。
- decor 瓦片只应出现在 `decor` 图层；画到 `ground` 层会因透明背景露出清屏色。
- loader 校验：id 1..65535 且不重复；每层数组长度 = `width*height`；图层引用的非 0 id 必须已声明。

## 8. 素材生成

贴图可用文生图模型（如 `wan2.7-image`）生成，再离线处理成目标尺寸。提示词按目标图层二选一：

- **ground 层（满幅不透明）**：top-down 2D pixel art tile，草地底、16-bit、平涂、统一光向、full-bleed、单格铺满。
- **decor 层（透明精灵）**：单个物件、纯洋红底 `#FF00FF`、无草地 / 无地面 / 无阴影。

处理：ground 裁黑边 → 中心方裁 → 缩到 16×16；decor 边缘泛洪抠洋红 → 裁包围盒 → 等比放入透明画布（水平居中、贴底）。
QA：与 grass 并排对比风格 / 色相 / 描边；decor 精灵应有相当比例透明像素。生成被 API 拒绝（额度 / 鉴权）时停止并汇报，不重试轰炸。

## 9. 新增放置对象（entities.json）

需要行为逻辑的组件走这条路，涉及代码处缺一不可：

1. `data/entities.json`：一行 `{id, kind, map, position|cell, texture, tint, payload, ...}`；
2. `parseEntitySpawns` 的 `knownKind` 白名单加新 kind（否则整行被跳过）；
3. `GameScene::buildWorld` 增加按 kind 生成实体的分支（Sprite / Collider / Interactable）；
4. 编辑器调色板：人物类在 `editorPlacementKinds()`（「对象」面板）；家具 / 危险类在 `openVisualEditor` 的
   `setComponentKinds`（「组件」面板）注册；
5. `saveEntitySpawns` 的 kind 映射（编辑器写回时保留字段）；
6. 有存档需求的，接入 M25 的 world 段读写；
7. F3 面向瓦片面板要显示：`facingTileDebugLines` 的 props 表加一项；
8. 编辑器「实体」页签属性行：`buildEditorEntityInfos` 加 kind 分支（键名 = JSON 字段名）。

## 10. 区域：碰撞区与耕地区

「区域」作用域先选新建类型，再点两格框选。

**碰撞区（`blockers`）**：框选的格子从此阻挡通行；给它选一张贴图（同瓦片面板的 `<` / `>` 循环），贴图锚定框选区底边、
`贴图高 N 格`可向上加高——判定只在框内，贴图高度不受框限制。想让角色进入：勾选「门 / 可进入」（整区放行），
或叠一个传送门（被传送门覆盖的格子自动解除阻挡，见 `Tilemap::blockerAt`）。存进地图 JSON：

```json
{ "id": "zone_1", "bounds": [8, 5, 6, 1], "texture": "tex.ground.wall",
  "render_height": 3, "passable": false }
```

`bounds` 必须在地图内；`render_height` 1..256；`texture` 为 catalog 贴图 id，留空表示「只判定、不画」。
贴图句柄由合成根按名字绑定，受第 6 节双源与色块回落约束。

**耕地区（`areas`，`kind = "farm_zone"`）**：框选矩形登记为一个 `MapArea`，区内普通地面（草地）自动可耕——
锄头在区内开垦、区外不行；已有 `tillable` 瓦片不受影响。判定入口 `Tilemap::isTillable`
（`anyDefHas(tillable) || farmZoneAt(cell)`），M19 农事自动生效。JSON：

```json
{ "id": 1, "kind": "farm_zone", "bounds": [8, 5, 6, 4] }
```

`id` 为正整数且地图内唯一，编辑器自动取下一空号。编辑器叠层：传送门绿、碰撞区红、耕地区棕。

## 11. 验证与提交

```text
MSBuild 三配置（Debug/Development/Release x64）0 error 0 warning
fa_tests            全部用例通过
fa_app              无参数烟测 OK；m05 无 placeholder 之外的失败；m12 defs 计数符合预期
fa_app --play-test  exit 0、无 "-> bad"
手动：--play → 主菜单「地图编辑器」→ 选已有地图「编辑」或输入名称「创建并编辑」→ 组件面板出现新条目 → 放置 → 保存重载仍在
```

- 新增中文文案后跑 `tools/build_font_atlas.py` 重烘字体图集（缺字显示 `?`），确认 `ui_atlas.json` 的 `width/height` 与实际 PNG 一致。
- 提交粒度：贴图 PNG → catalog（双源）→ 地图 defs → 文档登记，一次提交一件事。

## 12. 地图集合：文件名即地图 id

`data/maps/` 下的每个 `*.json` 就是一张地图，id 为相对该目录去掉扩展名的路径
（`farm.json` → `farm`，`play/farm.json` → `play/farm`）。编辑器的地图列表、传送门
可选目标都从目录扫描得出，**新增地图不需要改任何代码**：主菜单「地图编辑器」→
浏览器里输入名称新建（支持一级子目录，如 `town/square`），或直接放入一份合法地图 JSON。

- 浏览器可「编辑」任意地图（编辑器改的是内存副本，点保存才写回该文件）；
  「删除」两次点击确认，当前所在地图不可删除。
- 新建名称字段走 KeyDown 物理键（字母/数字/`_` `-` `/`，Shift+`-` 得 `_`，回车创建），
  与调试控制台同款 IME 兜底：中文输入法吞掉 TextInput 时依然可输入。
- 传送门 `target_map` 写地图 id；id 对不上文件时游戏换图会回落到内置布局并打日志。

## 13. 约束

1. 只增不改：已有瓦片 id / 尺寸 / 旗标是存档与地图的契约；要改先说明影响面。
2. 双源同步：第 6 节两处 catalog 必须同一改动内完成。
3. demo 地图（`farm-scripted.json` / `forest.json`）的图层是测试资产，`--play-test` 对出生点、门、荆棘、池塘、可耕区有坐标断言，未全绿不得改动。
4. 回落优先于报错：任何新组件缺图时必须能以色块运行（catalog 三件套 `color`/`altColor`/`tile` 必填）。
