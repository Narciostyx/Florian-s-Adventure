#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fa/core/Math.hpp"
#include "fa/core/Result.hpp"
#include "fa/physics/TileCollision.hpp"
#include "fa/render/RenderConstants.hpp"
#include "fa/render/RenderTypes.hpp"
#include "fa/tilemap/TileDef.hpp"
#include "fa/tilemap/TileId.hpp"

namespace fa::tilemap {

/// \brief What a map region is used for.
///
/// Greenhouse is an area property, not a separate scene (ADR-0002), so M19 reads it
/// from the map and never hard-codes coordinates.
enum class AreaKind : std::uint8_t {
    Outdoor,
    Greenhouse,
    Count,
};

/// \brief Stable identifier name for logs, editors and JSON.
[[nodiscard]] std::string_view toString(AreaKind kind) noexcept;

/// \brief A named region of the map, in tile coordinates.
struct MapArea {
    std::uint32_t areaId = 0;
    AreaKind kind = AreaKind::Outdoor;
    TileBounds bounds{};
};

/// \brief A doorway to another map, in tile coordinates.
struct MapPortal {
    std::string id;
    /// \brief Cells that trigger the transition.
    TileBounds bounds{};
    /// \brief Stable map id the transition leads to.
    std::string targetMap;
    /// \brief Cell the player appears on after the transition.
    TileCoord targetTile{};
};

/// \brief Map-wide environment values consumed by M22 and M08.
struct MapAmbient {
    /// \brief Indoor maps do not follow the outdoor day/night curve.
    bool indoor = false;
    /// \brief Multiplied with the world colour; white means "no tint".
    core::Color light{1.0f, 1.0f, 1.0f, 1.0f};
    /// \brief Music track id, empty when the map wants silence.
    std::string music;
};

/// \brief One drawable grid of tile ids.
///
/// `tiles` is row-major and must be exactly `width * height` long; the constructor
/// value is zero-filled, so an empty cell is the default rather than a sentinel.
struct TileLayer {
    std::string name;
    /// \brief Paint order among layers that share one `renderLayer`.
    int zOrder = 0;
    /// \brief Which M06 painter's layer this map layer belongs to.
    render::RenderLayer renderLayer = render::RenderLayer::Ground;
    bool visible = true;
    std::vector<TileId> tiles;
};

/// \brief Aggregates for the debug overlay and the smoke summary.
struct TilemapStats {
    int width = 0;
    int height = 0;
    int tileSize = 0;
    std::size_t layers = 0;
    std::size_t tileDefs = 0;
    std::size_t areas = 0;
    std::size_t portals = 0;
    /// \brief Cells with at least one non-empty tile across all layers.
    std::size_t nonEmptyTiles = 0;
    /// \brief Cells whose combined defs block movement.
    std::size_t solidTiles = 0;
    /// \brief Cells with at least one tillable def.
    std::size_t tillableTiles = 0;
    /// \brief Cells with at least one water def.
    std::size_t waterTiles = 0;
};

/// \brief A grid map: layers of tile ids, tile definitions, areas and portals.
///
/// This is plain data with no OpenGL, no ECS and no file access. It answers the
/// questions terrain, collision and gameplay need (`tileAt`, `isWalkable`,
/// `worldToGrid`, `queryArea`) and nothing else. Rendering is a separate extraction
/// pass (`extractTiles`) so a test can assert draw items without a window.
///
/// The type is copyable and movable on purpose: a loader returns it by value and a
/// save system can snapshot it. The `physics::ISolidTiles` bridge is a separate
/// non-owning adapter (`TilemapSolids`), which is what keeps that property.
class Tilemap {
public:
    /// \brief Largest accepted width or height, to reject corrupt documents early.
    static constexpr int kMaxDimension = 4096;

    Tilemap() = default;

    /// \brief Builds an empty map of the given extent. Invalid sizes clamp to zero.
    explicit Tilemap(int width, int height, int tileSize = render::kTileSize);

    /// \brief Replaces every layer, definition, area and portal with an empty map.
    ///
    /// \return `kInvalidDimensions` or `kInvalidTileSize` when the arguments are not usable.
    [[nodiscard]] core::Status reset(int width, int height, int tileSize = render::kTileSize);

    [[nodiscard]] int width() const noexcept { return m_width; }
    [[nodiscard]] int height() const noexcept { return m_height; }
    [[nodiscard]] int tileSize() const noexcept { return m_tileSize; }
    [[nodiscard]] bool empty() const noexcept { return m_width <= 0 || m_height <= 0; }
    [[nodiscard]] std::size_t cellCount() const noexcept;

    /// \brief World position of grid cell (0, 0); normally the map's top-left.
    [[nodiscard]] core::Vec2 origin() const noexcept { return m_origin; }
    void setOrigin(core::Vec2 origin) noexcept { m_origin = origin; }

    /// \brief Whether cells outside the grid block movement. Default true.
    [[nodiscard]] bool outsideIsSolid() const noexcept { return m_outsideIsSolid; }
    void setOutsideIsSolid(bool solid) noexcept { m_outsideIsSolid = solid; }

    /// \brief Adds a tile definition. \return false when the id is zero or already used.
    bool addTileDef(TileDef def);

    /// \brief Binds a loaded texture to an existing definition.
    ///
    /// \param source Sub-rectangle of the texture; empty means the whole texture.
    /// \return false when no definition with `id` exists.
    bool setTileTexture(TileId id, render::TextureHandle texture, core::Rect source = {});

    [[nodiscard]] const TileDef* tileDef(TileId id) const noexcept;
    [[nodiscard]] std::span<const TileDef> tileDefs() const noexcept { return m_tileDefs; }
    [[nodiscard]] std::size_t tileDefCount() const noexcept { return m_tileDefs.size(); }

    /// \brief Appends a layer. The tile array must already be `width * height` long.
    /// \return Layer index, or -1 when the name is empty/duplicated or the size is wrong.
    int addLayer(TileLayer layer);

    /// \return Layer index, or -1. Names are compared exactly.
    [[nodiscard]] int findLayer(std::string_view name) const noexcept;

    [[nodiscard]] TileLayer* layerAt(int index) noexcept;
    [[nodiscard]] const TileLayer* layerAt(int index) const noexcept;
    [[nodiscard]] int layerCount() const noexcept { return static_cast<int>(m_layers.size()); }
    [[nodiscard]] std::span<const TileLayer> layers() const noexcept { return m_layers; }

    [[nodiscard]] bool contains(int column, int row) const noexcept;

    /// \return The tile, or `kEmptyTile` when the cell or layer is out of range.
    [[nodiscard]] TileId tileAt(int column, int row, int layerIndex) const noexcept;

    /// \return false when the cell or layer is out of range.
    bool setTile(int column, int row, int layerIndex, TileId id) noexcept;

    /// \brief Fills every cell of one layer. \return false for a bad layer index.
    bool fillLayer(int layerIndex, TileId id) noexcept;

    /// \brief Paints the outermost ring of one layer. \return false for a bad index.
    bool setBorder(int layerIndex, TileId id) noexcept;

    /// \brief Whether every non-empty def on the cell allows walking.
    ///
    /// Out-of-grid cells are walkable only when `outsideIsSolid()` is false.
    [[nodiscard]] bool isWalkable(int column, int row) const noexcept;

    /// \brief Whether any def on the cell is water. Out-of-grid cells are dry.
    [[nodiscard]] bool isWater(int column, int row) const noexcept;

    /// \brief Whether any def on the cell can be tilled. Out-of-grid cells are not.
    [[nodiscard]] bool isTillable(int column, int row) const noexcept;

    /// \brief Whether any def on the cell is a wall.
    ///
    /// Out-of-grid cells follow `outsideIsSolid()`.
    [[nodiscard]] bool isSolidCell(int column, int row) const noexcept;

    /// \brief Grid cell containing a world point. Uses floor, so negatives work.
    [[nodiscard]] TileCoord worldToGrid(core::Vec2 world) const noexcept;

    /// \brief World-space top-left corner of a cell.
    [[nodiscard]] core::Vec2 gridToWorld(TileCoord coord) const noexcept;

    /// \brief World-space rectangle of a cell.
    [[nodiscard]] core::Rect tileRect(TileCoord coord) const noexcept;

    /// \brief World-space rectangle covering the whole map.
    [[nodiscard]] core::Rect boundsRect() const noexcept;

    void addArea(MapArea area);
    [[nodiscard]] std::span<const MapArea> areas() const noexcept { return m_areas; }

    /// \brief Kind of the first area containing `coord`, or nullopt.
    [[nodiscard]] std::optional<AreaKind> queryArea(TileCoord coord) const noexcept;

    /// \brief The first area containing `coord`, or null.
    [[nodiscard]] const MapArea* areaAt(TileCoord coord) const noexcept;

    void addPortal(MapPortal portal);
    [[nodiscard]] std::span<const MapPortal> portals() const noexcept { return m_portals; }

    /// \brief The first portal whose trigger area contains `coord`, or null.
    [[nodiscard]] const MapPortal* portalAt(TileCoord coord) const noexcept;

    [[nodiscard]] const MapAmbient& ambient() const noexcept { return m_ambient; }
    void setAmbient(MapAmbient ambient) noexcept { m_ambient = std::move(ambient); }

    [[nodiscard]] TilemapStats stats() const noexcept;

private:
    [[nodiscard]] const TileDef* defFor(TileId id) const noexcept;
    [[nodiscard]] bool anyDefHas(int column, int row, bool TileDef::*member) const noexcept;

    int m_width = 0;
    int m_height = 0;
    int m_tileSize = render::kTileSize;
    core::Vec2 m_origin{0.0f, 0.0f};
    bool m_outsideIsSolid = true;
    std::vector<TileDef> m_tileDefs;
    std::vector<TileLayer> m_layers;
    std::vector<MapArea> m_areas;
    std::vector<MapPortal> m_portals;
    MapAmbient m_ambient;
};

/// \brief Non-owning `physics::ISolidTiles` view of a `Tilemap`.
///
/// The bridge is an adapter rather than a base class so `Tilemap` stays movable and
/// copyable. The adapter must not outlive the map it points at; `PhysicsSystem`
/// borrows it only for the lifetime of the system.
class TilemapSolids final : public physics::ISolidTiles {
public:
    explicit TilemapSolids(const Tilemap& map) noexcept : m_map(&map) {}
    explicit TilemapSolids(const Tilemap* map = nullptr) noexcept : m_map(map) {}

    void setTilemap(const Tilemap* map) noexcept { m_map = map; }
    [[nodiscard]] const Tilemap* tilemap() const noexcept { return m_map; }

    [[nodiscard]] float tileSize() const noexcept override;
    [[nodiscard]] bool isSolid(int column, int row) const noexcept override;
    [[nodiscard]] int columns() const noexcept override;
    [[nodiscard]] int rows() const noexcept override;

private:
    const Tilemap* m_map = nullptr;
};

}  // namespace fa::tilemap
