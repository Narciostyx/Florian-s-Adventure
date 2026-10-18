# M12 Tilemap & World

| 项 | 值 |
|---|---|
| 层 | L2 |
| 优先级 | P1 |
| 状态 | 已实现 |
| 依赖 | M00–M11、M13 |
| 被依赖 | M13、M14、M17、M19、M22、M26 |

## 1. 目的

提供瓦片地图数据、图层、瓦片属性、区域、传送点与地图查询，是地形、碰撞、
耕地与场景摆放的共同基础。运行时只保存数据与查询结果；绘制由纯数据提取产出，
物理通过非 owning 适配器接入，因此地图可以被无窗口测试、存档和编辑器直接使用。

## 2. 范围

**负责**

- `Tilemap`：宽高、瓦片尺寸、多图层、瓦片 id 与定义表。
- 瓦片属性：可行走、实心、水面、可耕地、内容标签。
- 地图查询：世界坐标 ↔ 网格坐标、`tileAt`、`isWalkable`、`isWater`、
  `isTillable`、`isSolidCell`、`queryArea`、`portalAt`。
- 图层提取：按 `zOrder` 与可见区域提交 `render::DrawItem`。
- 自动瓦片：4 邻接 16 变体，加载/编辑时烘焙。
- 地图 JSON：图层、瓦片定义、区域、传送点、环境属性。
- `physics::ISolidTiles` 适配器，供 M13 直接碰撞。

**不负责**

- 碰撞求解（M13）。
- 耕地/作物状态（M19 的 `WorldObject` 组件）。
- 地图编辑 UI（M26）。
- 寻路（M21 可基于瓦片数据实现）。
- 纹理加载与句柄生命周期（M05 / composition root）。

## 3. 核心概念与数据结构

命名空间是 `fa::tilemap`，不是草案里的 `fa::world`：`world` 与 M11 的
`scene::World` 同名，容易在 include 与日志里混淆；日志分类本来就是 `Tilemap`。

```cpp
struct TileId { std::uint16_t value = 0; };   // 0 = 空
struct TileCoord { int x = 0; int y = 0; };
struct TileBounds { int x, y, width, height; }; // 半开区间

struct TileDef {
    TileId id;
    std::string name;
    std::string textureName;       // JSON 里的资源名，加载阶段不绑定
    render::TextureHandle texture; // composition root 通过 setTileTexture 绑定
    core::Rect source;             // 图集子矩形；空 = 整张纹理
    bool walkable = true;
    bool solid = false;            // 与 walkable 分开：水不是墙
    bool water = false;
    bool tillable = false;
    std::string tag;
};

struct TileLayer {
    std::string name;
    int zOrder = 0;
    render::RenderLayer renderLayer = render::RenderLayer::Ground;
    bool visible = true;
    std::vector<TileId> tiles;     // row-major，长度必须 = width * height
};

struct MapArea { std::uint32_t areaId; AreaKind kind; TileBounds bounds; };
struct MapPortal { std::string id; TileBounds bounds; std::string targetMap; TileCoord targetTile; };
struct MapAmbient { bool indoor; core::Color light; std::string music; };
```

- 网格坐标 `(x, y)` 原点在左上，x 向右、y 向下；世界坐标 =
  `origin + (x * tileSize, y * tileSize)`。
- 超出边界的查询不抛异常：`tileAt` 返回空瓦片，`isSolidCell` 返回
  `outsideIsSolid()`（默认 true，给有限地图外圈实心墙）。
- `Tilemap` 是**值类型**，可拷贝、可移动。`TilemapSolids` 是单独的非 owning
  适配器，实现 `physics::ISolidTiles`；这样 JSON loader 可以按值返回 `Tilemap`，
  而物理系统只借用适配器。
- `isWalkable` 的规则：任何图层上有 `solid` 或 `!walkable` 的瓦片都不可走；
  `isSolidCell` 只看 `solid`。水因此可以“不可走但不阻挡”。

### 瓦片基准与区域（ADR-0003 / ADR-0002 决策 2.2）

- `kTileSize = 16`；世界坐标 → 网格坐标使用 `floor`，负坐标也正确。
- `AreaKind` 目前是 `Outdoor` / `Greenhouse`；温室是区域属性，不是独立场景。
- `queryArea` 返回第一个包含该格的区域；区域数据随地图 JSON 的 `areas` 加载。
- `MapPortal` 用格子矩形描述触发区域，目标地图与目标格子都存稳定 id / 坐标。

## 4. 公共接口

主要头文件：`fa/tilemap/Tilemap.hpp`、`TileDef.hpp`、`TileId.hpp`、
`TilemapExtract.hpp`、`TilemapLoader.hpp`、`Autotile.hpp`、`TilemapError.hpp`。

```cpp
class Tilemap {
public:
    explicit Tilemap(int width, int height, int tileSize = render::kTileSize);
    core::Status reset(int width, int height, int tileSize = render::kTileSize);

    bool addTileDef(TileDef def);
    bool setTileTexture(TileId id, render::TextureHandle texture, core::Rect source = {});
    int addLayer(TileLayer layer);
    TileId tileAt(int column, int row, int layerIndex) const noexcept;
    bool setTile(int column, int row, int layerIndex, TileId id) noexcept;
    bool fillLayer(int layerIndex, TileId id) noexcept;
    bool setBorder(int layerIndex, TileId id) noexcept;

    bool isWalkable(int column, int row) const noexcept;
    bool isWater(int column, int row) const noexcept;
    bool isTillable(int column, int row) const noexcept;
    bool isSolidCell(int column, int row) const noexcept;

    TileCoord worldToGrid(core::Vec2 world) const noexcept;
    core::Vec2 gridToWorld(TileCoord coord) const noexcept;
    core::Rect tileRect(TileCoord coord) const noexcept;
    core::Rect boundsRect() const noexcept;

    std::optional<AreaKind> queryArea(TileCoord coord) const noexcept;
    const MapPortal* portalAt(TileCoord coord) const noexcept;
};

class TilemapSolids final : public physics::ISolidTiles {
public:
    explicit TilemapSolids(const Tilemap& map) noexcept;
    // tileSize / isSolid / columns / rows 转发给地图
};

TileExtractStats extractTiles(const Tilemap& map, render::RenderQueue& queue,
                              const TileExtractOptions& options = {});
class TilemapExtractSystem final : public scene::ISystem { ... };

core::Result<Tilemap> loadTilemapFromJson(std::string_view json);
std::string saveTilemapToJson(const Tilemap& map);

struct AutotileRule {
    TileId base;
    std::array<TileId, 16> variants;
    TileId variantFor(NeighborMask mask) const noexcept;
};
NeighborMask neighborMask(const Tilemap&, int column, int row, int layerIndex, TileId match) noexcept;
std::size_t bakeAutotiles(Tilemap& map, int layerIndex, const AutotileRule& rule);
```

### 渲染提取

- `extractTiles` 按 `zOrder` 升序遍历可见图层，平手按插入顺序。
- `TileExtractOptions::cullToViewBounds` 打开后只访问与 `viewBounds` 相交的格子；
  `TilemapExtractSystem::setViewBounds(camera.visibleRect())` 是 app 的接法。
- 无纹理的瓦片默认跳过（`skipWithoutTexture = true`），所以地图可以先于图集加载。
- 每个 `DrawItem` 的 `sortY` 是格子底边；`entityId` 按提交顺序递增，使共享
  `render::RenderLayer` 的多个地图图层在排序后仍有确定覆盖关系。
- 提取是纯数据，不需要 GL 上下文，测试直接断言 `RenderQueue`。

### 自动瓦片

- 4 邻接掩码：North=1、East=2、South=4、West=8；地图外按“匹配”处理。
- `AutotileRule::variants[mask]` 为空时回退到 `base`。
- `bakeAutotiles` 先快照图层，再计算掩码，所以写入顺序不影响结果；编辑后需要重新烘焙。

## 5. 依赖关系

- 直接引用 `fa_core`、`fa_time`、`fa_render`、`fa_scene`、`fa_physics`。
- **不引用 `fa_assets`**：`TileDef` 只存裸 `render::TextureHandle`，
  JSON 里的 `texture` 是资源名，由 composition root 解析后调用 `setTileTexture()`。
  这样 `fa_tilemap` 不会把资源管理器的 JSON 与 `/EHsc` 依赖向下拖给每个使用方。
- **不反向依赖 M13**：地图实现/适配 `physics::ISolidTiles`，物理不需要知道瓦片结构；
  `fa_physics` 仍然不引用 `fa_tilemap`。
- JSON loader 使用 nlohmann/json，`fa_tilemap.vcxproj` 因此单独打开 `/EHsc`；
  没有 `fa_tilemap` 代码 throw/catch。
- M17/M19 修改耕地等状态时走独立覆盖层或组件，不直接改渲染数据。

## 6. JSON 文档格式

```json
{
  "schema_version": 1,
  "width": 3,
  "height": 2,
  "tile_size": 16,
  "origin": [8, 4],
  "outside_solid": false,
  "tiles": [
    {"id": 1, "name": "grass", "texture": "tex.grass", "source": [0, 0, 16, 16],
     "walkable": true, "solid": false, "tillable": true, "tag": "ground"},
    {"id": 2, "name": "wall", "texture": "tex.wall", "walkable": false, "solid": true}
  ],
  "layers": [
    {"name": "ground", "z_order": 0, "render_layer": "ground",
     "tiles": [1, 1, 1, 2, 1, 1]},
    {"name": "detail", "z_order": 5, "render_layer": "ground_detail",
     "visible": true, "tiles": [0, 0, 0, 0, 0, 0]}
  ],
  "areas": [
    {"id": 1, "kind": "greenhouse", "bounds": [0, 0, 2, 1]}
  ],
  "portals": [
    {"id": "to_town", "bounds": [2, 1, 1, 1], "target_map": "town", "target_tile": [5, 6]}
  ],
  "ambient": {"indoor": true, "light": [0.5, 0.5, 0.5, 1.0], "music": "greenhouse"}
}
```

- `layers[].tiles` 是 row-major 扁平数组，长度必须正好 `width * height`；
  非零值必须在 `tiles` 中声明，否则是 `kUnknownTileId`。
- `render_layer` 接受 `background`、`ground`、`ground_detail`、`objects`、
  `actors`、`foreground`、`weather`、`ui`。
- `areas` / `portals` 的 `bounds` 必须是非空且在 map 内。
- 未知字段忽略；缺失必填字段、类型错误、重复 id、越界数据返回 `core::Result` 错误。

## 7. 实现与测试

代码位置：

```text
src/engine/include/fa/tilemap/   TileId, TileDef, Tilemap, TilemapExtract,
                                 TilemapLoader, TilemapWriter, Autotile, TilemapError
src/engine/tilemap/src/          Tilemap.cpp, TilemapExtract.cpp,
                                 TilemapLoader.cpp, TilemapWriter.cpp, Autotile.cpp
```

测试：`tests/tilemap.test.cpp`，17 个用例 / 275 条断言，覆盖：

- `TileId` 空值语义、`TileBounds` 半开区间；
- 构造/重置、非法尺寸、图层尺寸与重名、图层访问；
- 世界坐标 ↔ 网格坐标（含负数与 origin）、边界查询；
- 跨图层 walkable/solid/water/tillable 组合、地图外语义；
- 区域首匹配、传送点查询、环境属性；
- JSON 完整文档与 15 条错误路径断言；
- 自动瓦片掩码、快照烘焙；
- 提取的提交数、空瓦片/无纹理跳过、图层顺序、可见性、视锥裁剪与统计；
- `TilemapSolids` 与 `PhysicsWorld::move` 的端到端阻挡（标记停在 x=24）。

冒烟集成：`DemoScene` 用 40×23 地图替换了原来的 920 个地面实体，地图
自带地面/墙/耕地三种瓦片；`m12:` 行报告 920 格、143 实心、16 可耕地、
920 个提交项、0 裁剪。物理仍由 M13 求解，标记撞墙停在 x=344。

## 8. 扩展点

- 新增图层类型：扩展 `TileLayer::renderLayer` 映射即可，`zOrder` 已支持同层多图层。
- 新增自动瓦片规则：`AutotileRule` 是数据，未来可替换为策略对象（47 变体 / Wang）。
- 大地图分块：`Tilemap` 可以增加 chunk 管理，`tileAt` / `extractTiles` 接口不变。
- Tiled 导入：`loadTilemapFromJson` 可以作为一个独立 loader，运行时格式保持稳定。
- M26 编辑器：直接编辑 `TileLayer::tiles` 与 `TileDef`，用 `saveTilemapToJson`
  保存为同一 JSON；往返由 `tests/editor.test.cpp` 覆盖。

## 9. 风险与开放问题

- `defFor` 是线性查找；单地图瓦片定义通常 < 100，若内容表膨胀到数百种需要考虑索引。
- `stats()` 会遍历所有图层所有格子；调试/冒烟可用，不应每帧调用。
- 全量载入内存：128×128×3 层可接受；更大需要分块。
- 自动瓦片在编辑后必须重新烘焙，运行时不自动重算。
- 传送点目前只是数据查询；真正切场景由 M25 消费。
- JSON 的图层数据是扁平数组，手写大图不便；M26 编辑器或导入器负责生成。
- `kVirtualHeight = 360` 仍是 22.5 格；演示地图取 23 行，相机夹取会裁掉底部
  半格。更大的地图要么按 23 行布局，要么改 `640×352`（M10/M12）。
- 纹理绑定按名称由 composition root 完成；地图 JSON 不会自动加载纹理。

