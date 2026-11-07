# M26 Visual Editor（设计扩展）

| 项 | 值 |
|---|---|
| 层 | L4 |
| 优先级 | P2 |
| 状态 | P0/P1 与 P2 瓦片属性已实现；实体/区域放置待做 |
| 依赖 | M00–M25（`fa_core`、`fa_tilemap`、`fa_render`、`fa_ui`、M05 资源、M11 ECS、M15 内容） |
| 关系 | 扩展 [M26 Editor Tools](26-editor-tools.md) 的面板层；复用 `MapEditor`/`EditorHistory` |

> 现有 M26 只有瓦片笔刷和固定坐标的简易面板。本文定义一个完整的可视化编辑器：
> 左侧地图、右上组件库、右下属性检查器，所有数据与 `data/*.json` 双向同步，
> 并通过描述符注册表保证新增组件/属性类型不需要改编辑器核心。

## 1. 目标

1. **可视化编辑地图**：平移/缩放、网格、图层、笔刷/矩形/填充/擦除/取色。
2. **放置组件**：从组件库把瓦片、实体、敌人、NPC、传送点、区域放到地图上。
3. **属性检查器**：编辑文档级属性（地图大小、环境光、音乐）与选中对象属性
   （如草地的 `texture` 路径、`walkable`、`solid`）。
4. **JSON 双向一致**：读取与运行时相同的 `data/maps/*.json`、`data/entities.json`、
   `data/catalog.json`、`data/content.json`；保存后运行时可直接加载。
5. **可拓展**：新增一种组件或一种属性类型只需注册描述符，不改面板/命令/序列化代码。

## 2. 非目标

- 不做外部美术制作；只引用 `catalog.json` 里的纹理。
- 不做运行时发布 UI（仅 Debug/Development 编译）。
- 不做通用脚本/表达式编辑器。
- 不做多人协同编辑；冲突检测只到“文件被外部修改”这一级。

## 3. 界面布局

```text
┌──────────────────────────────────────────────────────────────────────────┐
│ 工具栏：新建/打开 | 撤销/重做 | 网格 | 缩放 | 校验 | 保存 | 重新加载运行时 │
├──────────────────────────────────────────┬───────────────────────────────┤
│                                          │ 组件库（右上方）               │
│                                          │ ┌───────────────────────────┐ │
│                                          │ │ [瓦片][实体][敌人][区域]   │ │
│           地图视图（左侧）                │ │ 草地  █  墙  █  土  █      │ │
│                                          │ │ 传送点 ◇ 温室 ▧ 敌人 ◆     │ │
│   · 平移 / 缩放 / 网格吸附                │ └───────────────────────────┘ │
│   · 悬停高亮 + 坐标/瓦片提示              ├───────────────────────────────┤
│   · 框选、放置预览、非法位置红色          │ 属性（右下方）                 │
│                                          │ ┌───────────────────────────┐ │
│                                          │ │ 文档：maps/farm.json       │ │
│                                          │ │ 尺寸 40 × 23  瓦片 16 px   │ │
│                                          │ │ 室外实心 / 环境光 / 音乐    │ │
│                                          │ ├───────────────────────────┤ │
│                                          │ │ 选中：瓦片 #1 草地         │ │
│                                          │ │ 贴图 tex.ground.grass      │ │
│                                          │ │ 可走 ✓  实心 □  可耕 □     │ │
│                                          │ │ 标签 ground  源矩形 0,0…    │ │
│                                          │ └───────────────────────────┘ │
├──────────────────────────────────────────┴───────────────────────────────┤
│ 状态栏：格子 (12,7) | 图层 ground | 未保存 | 3 个校验警告                 │
└──────────────────────────────────────────────────────────────────────────┘
```

- 最小 1280×720；左/右默认 70% / 30%，可拖动分隔条，宽度写入用户配置。
- 右侧上下两栏可折叠；组件库与属性检查器各自独立滚动。
- 所有面板由 M09 即时模式 UI 绘制，输入走 `InputContext::Editor`。

## 4. 交互模型

| 操作 | 行为 |
|---|---|
| 中键拖拽 / 空格+左键 | 平移地图 |
| 滚轮 | 以光标为中心缩放（0.5×–4×） |
| 左键 | 使用当前组件/工具；拖动为连续笔刷 |
| 右键 | 擦除；在组件库中为“取色” |
| `B/R/F/E/P` | 笔刷/矩形/填充/擦除/取色 |
| `Ctrl+Z` / `Ctrl+Y` | 撤销/重做 |
| `Ctrl+S` | 保存当前文档（原子写 + `.bak`） |
| `Delete` | 删除选中组件 |
| `Esc` | 取消当前拖拽/框选，不提交命令 |
| `F1` | 打开快捷键帮助 |

- 拖拽期间只显示预览，松开鼠标才生成一条命令（整条笔刷/矩形可一次撤销）。
- 放置前调用 `IDocumentAdapter::validatePlacement()`；非法位置红色预览并禁用提交。
- 多选（Shift 点击/框选）时属性面板显示公共字段，混合值显示 `—`，修改会应用到全部选中项。

## 5. 数据模型与 JSON 映射

### 5.1 文档模型

```cpp
enum class DocumentKind { Map, Entities, Catalog, Content };

struct EditorDocument {
    DocumentKind kind;
    std::string path;             // 相对仓库根：data/maps/farm.json
    SaveArchive root;             // 原始 JSON 树，未知字段原样保留
    bool dirty = false;
    std::uint64_t revision = 0;   // 每次命令 +1，用于运行时热重载
};

struct Selection {
    DocumentKind kind;
    std::string arrayPath;        // "tiles" / "layers/0/tiles" / "entities"
    std::size_t index = 0;        // 数组下标；-1 表示文档级选择
    std::string componentId;      // 描述符 id，如 "entity.enemy"
};
```

- 保存时只回写已知字段，未知字段（未来版本/手工注释之外的键）原样保留。
- 文档级属性与选中属性都通过 `PropertyBag` 读写，不直接操作指针。

### 5.2 地图 `data/maps/*.json`

| JSON 字段 | 编辑器位置 | 说明 |
|---|---|---|
| `schema_version` | 文档属性（只读） | 保存时保持当前版本 |
| `width` / `height` / `tile_size` | 文档属性 | 改尺寸时按图层重排 `tiles`，越界内容需确认丢弃/裁剪 |
| `origin` / `outside_solid` | 文档属性 | 世界原点与图外碰撞 |
| `ambient.indoor` / `light` / `music` | 文档属性 | 室内标记、环境光颜色、音乐 clip id |
| `tiles[]` | 组件库「瓦片」+ 属性 | 瓦片定义：`id`、`name`、`texture`、`source`、`solid`、`walkable`、`tillable`、`water`、`tag` |
| `layers[]` | 组件库「图层」+ 文档属性 | `name`、`render_layer`、`z_order`、`visible`、`tiles[]` |
| `portals[]` | 组件库「传送点」+ 属性 | `id`、`bounds`、`target_map`、`target_tile` |
| `areas[]` | 组件库「区域」+ 属性 | `id`、`kind`（如 `greenhouse`）、`bounds` |

瓦片属性示例（右侧面板直接编辑）：

```json
{
  "id": 1,
  "name": "grass",
  "texture": "tex.ground.grass",
  "source": [0, 0, 0, 0],
  "solid": false,
  "walkable": true,
  "tillable": false,
  "water": false,
  "tag": "ground"
}
```

- `texture` 使用资源选择器，下拉项来自 `data/catalog.json` 的 `texture` 条目；
  选择后地图预览立即重绑纹理，保存时只写 id。
- `id` 被 `layers[].tiles[]` 引用；删除瓦片定义前必须检查引用并提示。
- 地图尺寸变化会重建每个图层的 `tiles` 数组；旧内容按左上角对齐保留。

### 5.3 纹理目录 `data/catalog.json`

| JSON 字段 | 属性面板 | 说明 |
|---|---|---|
| `id` | 只读 | 被地图/内容引用 |
| `kind` | 枚举 | `texture` / `json` |
| `path` | 文件选择 | 相对 `assets/`；空表示纯色占位 |
| `size` | 两整数 | 占位尺寸 |
| `color` / `altColor` / `tile` | 颜色/整数 | 占位颜色与棋盘格 |

保存后触发 M05 重新加载该纹理；加载失败在状态栏显示，不阻塞其他文档。

### 5.4 实体与敌人 `data/entities.json`

| JSON 字段 | 属性面板 | 说明 |
|---|---|---|
| `id` | 自动生成 | 唯一；敌人同时作为存档 role。M09 没有文本输入控件，所以放置时按 kind 生成 `npc_1`… |
| `kind` | 组件库选择 | `npc` / `bed` / `shipping` / `merchant` / `enemy` / `hazard` |
| `map` | 隐式（当前地图） | 行属于哪张地图；编辑器只重写当前地图的行 |
| `position` / `cell` | `宽±` / `高±` | 世界坐标或格子坐标；编辑器一律按格子写回 |
| `dialogue` | `目标 < … >` 循环 | 仅 `npc`：候选来自 `narrative.json` 的 `dialogues[].id` |
| `texture` / `tint` | 资源/颜色 | 缺省用内置占位；`texture` 暂不在编辑器里改 |
| `payload` / `radius` | 整数/浮点 | 交互目标 |
| `health` / `loot` / `damage` | 整数/内容引用 | 敌人与危险格 |
| `sight_radius` / `attack_range` / `move_speed` / `attack_damage` / `attack_cooldown` | 数值 | 敌人 AI 调参 |

- 组件库「对象」按 `kind` 分组；左键按当前格子放置，重复点击同一格是选中而不是叠加。
- 序列化由组装根完成，**按 kind 只写该 kind 会读的字段**；未被编辑器触碰的行原样保留
  （包括它的 `position` 亚格子精度）。
- `loot` 用内容选择器（未实现），候选为 `content.json` 的 `loot_tables[]`。

### 5.5 内容 `data/content.json`

只读或受控编辑，供瓦片/实体属性做引用选择：

| 数组 | 用途 |
|---|---|
| `items[]` | 掉落、商店、配方、任务引用 |
| `crops[]` | 农田/种子/收获物 |
| `recipes[]` | 加工 |
| `enemies[]` | 敌人模板（放置时可复制默认值） |
| `loot_tables[]` | 实体 `loot` 选择器 |

编辑内容定义时，保存前校验所有引用（种子/产物/掉落表/配方输入）仍然存在。

### 5.6 其它只读面板

`shops.json`、`audio.json`、`narrative.json` 可先以只读树/表格展示，后续通过
相同的 `IDocumentAdapter` 接入编辑，不需要改面板核心。

## 6. 可拓展性设计

### 6.1 组件描述符

```cpp
enum class PropertyType {
    Int, Float, Bool, Enum, Text, Color, Vec2, Rect, Bounds,
    AssetRef, ContentRef, TileRef, StringList
};

struct PropertyDescriptor {
    std::string key;          // JSON 键，如 "texture"
    std::string label;        // 面板显示
    PropertyType type;
    std::string enumSource;   // Enum/Bounds 的候选来源
    bool required = false;
    std::string tooltip;
};

struct ComponentDescriptor {
    std::string id;           // "tile", "entity.enemy", "portal", "area"
    std::string category;     // "Tiles" / "Entities" / "Areas"
    DocumentKind document;    // 该组件写入哪类文档
    std::string arrayPath;    // "tiles" / "entities" / "portals" / "areas"
    std::vector<PropertyDescriptor> properties;
    std::function<core::Status(EditorDocument&, const PropertyBag&, Selection&)> place;
    std::function<core::Status(const EditorDocument&, Selection, PropertyBag&)> read;
};
```

- 新增组件：实现一个描述符并注册，组件库自动出现、属性面板自动生成。
- 新增属性类型：注册 `IPropertyEditor`，描述符即可引用；核心面板不改。
- 复杂组件（如多格建筑）在 `place` 里写多格占用，仍只产生一条命令。

### 6.2 文档适配器

```cpp
class IDocumentAdapter {
public:
    virtual ~IDocumentAdapter() = default;
    virtual DocumentKind kind() const noexcept = 0;
    virtual core::Status load(std::string_view path, EditorDocument& out) = 0;
    virtual core::Status save(EditorDocument& document) = 0;
    virtual core::Status validate(const EditorDocument&, std::vector<ValidationIssue>&) = 0;
    virtual std::span<const ComponentDescriptor*> palette() const = 0;
};
```

- `MapDocumentAdapter`：瓦片/图层/传送点/区域/地图属性。
- `EntitiesDocumentAdapter`：实体数组与 AI 字段。
- `CatalogDocumentAdapter`：纹理条目与 M05 重载。
- `ContentDocumentAdapter`：内容定义与引用校验。
- 适配器负责“索引 ↔ JSON 路径”的转换，面板只认 `Selection` 与 `PropertyBag`。

### 6.3 命令与撤销

```cpp
struct PropertyChange { std::string path; SaveArchive before; SaveArchive after; };

using EditorEdit = std::variant<TileChange, EntityChange, PropertyChange>;

struct EditorCommand {
    std::string label;            // "Paint 12 cells" / "Place enemy.slime"
    std::vector<EditorEdit> edits;
};
```

- 笔刷拖动合并为一条命令；属性输入框在失焦/回车时提交一条命令。
- 撤销/重做只回放差分，不整文件快照；大图仍保持低内存。
- 每条命令后 `document.revision++`，运行时在安全点按 revision 热重载。

### 6.4 校验

`ValidationIssue { Severity, document, path, message }`，至少覆盖：

- 瓦片 id 重复、被引用瓦片被删除；
- 图层 `tiles` 长度与 `width × height` 不符；
- 传送点 `target_map` 指向不存在的地图；
- 实体 id 重复、坐标越界、`loot`/`texture` 引用不存在；
- 内容引用（种子/产物/配方/掉落表）；
- 未知/缺失 `schema_version`。

错误显示在状态栏与「校验」面板；有 Error 时保存需二次确认，Warning 不阻塞。

### 6.5 保存与热重载

- 保存前写 `.tmp`，成功后轮转 `.bak`，与 M14 的原子写策略一致。
- 记录文件 mtime/hash；外部修改后保存会提示“重新加载/覆盖”。
- 保存成功后：
  - 地图：调用 M12 重新加载并重绑纹理；
  - 实体：重建当前场景；
  - 目录/内容：刷新 M05/M15 缓存并校验引用。
- 运行时无法热重载的字段（如地图尺寸变化）提示“需重启 play 会话”。

## 7. 架构与文件

| 文件 | 职责 |
|---|---|
| `fa/editor/EditorDocument.hpp` | 文档、选择、属性包、校验问题 |
| `fa/editor/ComponentRegistry.hpp` | 组件/属性描述符注册 |
| `fa/editor/DocumentAdapter.hpp` | `IDocumentAdapter` 与四类适配器接口 |
| `fa/editor/VisualEditor.hpp` | 布局、面板状态、命令分发 |
| `fa/editor/PropertyEditors.hpp` | 每种 `PropertyType` 的绘制与提交 |
| `src/editor/MapDocumentAdapter.cpp` | 地图 JSON 读写与尺寸重排 |
| `src/editor/EntitiesDocumentAdapter.cpp` | 实体数组读写 |
| `src/editor/VisualEditor.cpp` | 面板绘制与输入 |
| `src/app/main.cpp` | 注册适配器/描述符，F2 打开，保存后通知运行时 |

模块边界：

- M26 只依赖 M01/M05/M09/M11/M12/M15 的只读/窄接口，不直接调用玩法系统。
- 保存后的重载由组装根执行（与 `LoadHandler` 同层），避免编辑器耦合 M19/M20/M21。

## 8. 实施阶段

| 阶段 | 内容 | 验收 |
|---|---|---|
| P0 布局 | 左右分栏、组件库、属性面板骨架、状态栏 | 1280×720/1920×1080 下不重叠；可折叠/拖动分隔条 |
| P1 地图编辑 | 平移/缩放/网格、笔刷/矩形/填充/擦除/取色、撤销/重做、保存 | 编辑 `farm.json` 后运行时加载一致；`Ctrl+Z` 恢复 |
| P2 属性检查 | 地图属性 + 瓦片属性；资源选择器改 `texture`；尺寸调整 | 改草地贴图路径后预览/运行时生效；改尺寸后图层长度一致 |
| P3 组件放置 | 实体/敌人/传送点/区域放置与属性；内容引用选择器 | 拖入敌人并调 AI 字段，保存后 play 模式出现 |
| P4 校验/热重载 | 校验面板、外部修改检测、保存后热重载 | 删除被引用瓦片报错；外部改文件后保存提示 |

## 9. 测试

- 单元：`EditorDocument` 路径读写、尺寸重排、描述符注册、属性类型转换、命令撤销。
- 集成：用临时目录读写真实 JSON，保存后再用 M12/M15 加载，比较关键字段。
- UI：固定 1280×720 与 1920×1080，点击/拖拽脚本验证面板命中区域与滚动。
- 回归：`--frames 60`、`--play-test`、`--stress 1000` 不受编辑器关闭状态影响。

## 10. 实现状态

代码位置：

- `src/engine/include/fa/editor/VisualEditor.hpp`
- `src/engine/editor/src/VisualEditor.cpp`
- 入口：主菜单「地图编辑器」按钮（`src/app/main.cpp`，仅 `FA_ENABLE_EDITOR` 配置可见）。
  **注意**：它需要已存在的场景，冷启动（没开过游戏）时点击无反应；先「开始新游戏」→
  `Esc` → 「主菜单」→「地图编辑器」即可。

已实现（P0/P1 + P2 的瓦片属性 + P3 的对象与传送点）：

| 区域 | 内容 |
|---|---|
| 布局 | 左侧地图视口、右上组件库、右下属性检查器、顶部工具栏、状态文字 |
| **编辑 scope** | 工具栏最左是 `瓦片 / 对象 / 传送门` 三态；切换时清空选择与进行中的框选，非瓦片 scope 不绘制瓦片工具与撤销/重做（它们只对瓦片历史有意义） |
| 工具栏 | 瓦片 scope：`笔刷` / `橡皮` / `填充`、`撤销` / `重做`；对象 scope 显示对象数，传送门 scope 显示「共 N 个」；末尾 `返回` |
| 地图绘制 | 按瓦片层 `visible` 与 `renderLayer` 提交 `DrawItem`；16px 源图按视口等比缩放；悬停格高亮；**传送门触发区画半透明绿块 + 边框，已放置对象画橙色小块** |
| 输入 | 左键绘制、右键擦除；仅在地图视口（工具栏以下）内生效，不抢占右侧面板按钮 |
| **对象放置** | 组件库列出组装根提供的 kind（NPC 可对话 / 床 / 出货箱 / 商人 / 敌人 / 荆棘）；左键在空格放置、点已有对象则选中；右键删除；属性面板可 `宽±/高±` 挪格、`目标 < … >` 在对话图之间循环、`删除` |
| **传送门** | 点两格确定对角（与瓦片矩形工具一致）生成 `MapPortal`；组件库列出全部传送门可选择；属性面板可切 `通往` 目标地图、调落点格、`删除`；`共 N 个` 计数 |
| 尺寸 | `宽-`/`宽+`/`高-`/`高+` 调整地图大小，保留左上角区域、图层、瓦片定义、区域与传送点 |
| 地图属性 | `界外阻挡`、`室内`（`ambient.indoor`） |
| 瓦片属性 | 贴图名循环选择（来自 `catalog.json` 的 `Texture` 条目）、`可通行`、`阻挡`、`可耕`、`水` |
| **保存** | `保存`（在**所有 scope** 都绘制，切到对象/传送门不会没有出口）：写地图 JSON + 通过回调写 `data/entities.json`；地图写回**当前打开的那个文件**（不再硬编码 farm） |
| 字号 | 编辑器局部 `UiTheme::textScale = 1`、`padding = 2`：10 px 图集下这是唯一能塞进 11–13 px 行的倍率，默认 5 px padding 会把 20 px 的按钮内宽吃到 10 px |
| 关闭 | `返回` 或 `Esc`；控件状态与撤销历史保留 |

尚未实现（后续阶段）：

- P2 其余：地图 `origin`、`tileSize`、`ambient.light`、`ambient.music` 编辑。
- P3 其余：**区域（`MapArea`，如温室）的放置与属性**、对象 `texture` 选择、内容引用选择器、
  传送门 id 改名（M09 无文本输入控件，id 目前自动生成）。
- P4：校验面板、外部修改检测、保存后热重载、`Ctrl+Z/Ctrl+Y` 快捷键。
- 对象与传送门的编辑**不进撤销栈**（历史只记录瓦片 diff），只把会话标为未保存。

保存的 JSON 仍由 `TilemapWriter` 生成，因此与运行时 `loadTilemapFromJson` 的字段完全一致；
`--play-test` 的 `play-test: visual editor ok` 会用编辑器输入路径改一个格子、写盘并重新加载校验。
对象行由组装根用 `SaveArchive` 序列化，**按 kind 只写该 kind 真会读的字段**（床不会带 AI 参数），
且只重写当前地图 `map` 的行。

## 11. 风险与开放问题

- 地图尺寸变化会破坏引用坐标（传送点/区域/实体）；需要“同步平移/裁剪”策略确认。
- 属性面板的混合值与多选编辑规则需要产品确认。
- 热重载与 play 会话的时机：是否允许编辑时暂停游戏、保存后立即重建场景。
- `content.json` 的编辑是否开放给策划，还是只做引用浏览。
- 大图（128×128）下全量重绘面板的性能与滚动策略。