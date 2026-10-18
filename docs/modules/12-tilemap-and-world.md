# M12 Tilemap & World

| 项 | 值 |
|---|---|
| 层 | L2 |
| 优先级 | P1 |
| 状态 | 设计完成 |
| 依赖 | M00–M11 |
| 被依赖 | M13、M14、M17、M19、M22、M26 |

## 1. 目的

提供瓦片地图数据、图层、瓦片属性、自动瓦片与地图查询，是地形、碰撞、耕地与场景摆放的共同基础。

## 2. 范围

**负责**

- `Tilemap`：宽高、瓦片尺寸、多图层（Ground/Detail/Buildings）、瓦片 ID。
- 瓦片定义：`TileDef`（纹理、源矩形、是否可行走、是否水面、是否可耕地、标签）。
- 地图查询：世界坐标 ↔ 网格坐标、`tileAt`、`isWalkable`、`isTilled`。
- 图层渲染：只提交可见区域瓦片，按图层排序。
- 自动瓦片（autotile）：基于邻居位掩码选择变体；v1 支持 16 变体（4 位）或 47 变体子集。
- 地图文件：JSON 描述图层与瓦片，或 PNG + 图集映射；v1 用 JSON + 图集。
- 地图边界与传送点（通往其他地图）。
- 地图级环境属性（室内/室外、光照色、背景音乐）。

**不负责**

- 碰撞求解（M13 使用瓦片属性）。
- 耕地/作物状态（M19 的 `WorldObject` 组件）。
- 地图编辑 UI（M26）。
- 寻路（M21 可基于瓦片实现）。

## 3. 核心概念与数据结构

```cpp
struct TileId { std::uint16_t value = 0; }; // 0 = 空

struct TileDef {
    TileId id;
    AssetHandle<Texture> texture;
    glm::vec4 sourceRect;
    bool walkable = true;
    bool water = false;
    bool tillable = false;
    std::string tag;
};

struct TileLayer {
    std::string name;
    int zOrder;
    std::vector<TileId> tiles; // width * height
};

struct Tilemap {
    int width, height;
    int tileSize = 16;
    std::vector<TileLayer> layers;
    glm::vec2 worldOrigin{0.0f};
};
```

- 网格坐标 `(x, y)` 原点在左上，x 向右、y 向下；世界坐标 = 原点 + `(x * tileSize, y * tileSize)`。
- 超出边界的查询返回空瓦片，不抛异常。
- 自动瓦片变体在加载时烘焙，运行时不重新计算（除非编辑）。

### 瓦片基准与区域（ADR-0003 / ADR-0002 决策 2.2）

- `kTileSize = 16`；地图坐标 ↔ 格坐标互转使用整数除法向下取整。
- 新增区域概念：

```cpp
struct FarmArea {
    uint32_t areaId = 0;
    AreaKind kind = AreaKind::Outdoor;  // Outdoor | Greenhouse
    Recti tileBounds;
};
```

- 查询：`std::optional<AreaKind> queryArea(TileCoord tile) const;`
- 温室是区域属性，不是独立场景；M19 只读 `areaKind`，不硬编码坐标。
- 区域数据存于地图 JSON 的 `areas` 数组，随地图一起加载。

## 4. 公共接口草案

```cpp
namespace fa::world {

class ITilemap {
public:
    virtual ~ITilemap() = default;
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual int tileSize() const = 0;
    virtual TileId tileAt(int x, int y, int layer) const = 0;
    virtual bool isWalkable(int x, int y) const = 0;
    virtual glm::ivec2 worldToGrid(glm::vec2 world) const = 0;
    virtual glm::vec2 gridToWorld(glm::ivec2 grid) const = 0;
    virtual void render(render::IRenderer& renderer, const Camera2D& camera) const = 0;
};

} // namespace fa::world
```

## 5. 依赖关系

- 依赖 M01、M05（图集）、M06（绘制）、M11（地图实体/系统注册）。
- 不依赖 M13；M13 反向使用本模块——地图实现 `fa::physics::ISolidTiles`（`tileSize()` + `isSolid(column, row)`）即可接入碰撞，物理不需要知道瓦片结构。
- M17/M19 修改瓦片状态（耕地）时通过 `ITilemap` 的可变接口或独立覆盖层，避免直接改渲染数据。

## 6. 扩展点

- 新增图层类型（装饰、遮挡）只需扩展 `zOrder` 与渲染层映射。
- 新增自动瓦片规则集（16/47/王字）作为策略对象。
- 大地图分块加载：`Tilemap` 增加 chunk 管理，查询接口不变。
- 地图生成器：程序化生成或 Tiled 导入作为独立 loader。

## 7. 实现阶段

P1，Phase B。先实现单图层 JSON 地图、查询、可见区域渲染；再实现多图层、自动瓦片、传送点。验收：128×128 地图流畅滚动；碰撞与视觉一致；耕地层正确叠加。

## 8. 风险与开放问题

- 瓦片 16×16 还是 32×32 未定，影响美术与性能；接口用 `tileSize` 参数化。
- 自动瓦片规则复杂，v1 应限制为简单 4 位邻接。
- 大地图全量载入内存是否可接受？128×128×3 层可接受；更大需分块。
- 地图编辑数据格式与运行时格式是否统一？建议统一 JSON，M26 直接编辑。
