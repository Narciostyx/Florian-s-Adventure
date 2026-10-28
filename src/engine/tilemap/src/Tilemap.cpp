#include "fa/tilemap/Tilemap.hpp"

#include "fa/tilemap/TilemapError.hpp"

#include <cmath>
#include <limits>

namespace fa::tilemap {

namespace {

/// \brief Grid index of `value`, clamped so an extreme world point cannot overflow.
[[nodiscard]] int clampToGrid(float value) noexcept {
    const float floored = std::floor(value);
    if (floored <= static_cast<float>(std::numeric_limits<int>::min())) {
        return std::numeric_limits<int>::min();
    }
    if (floored >= static_cast<float>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(floored);
}

}  // namespace

std::string_view toString(AreaKind kind) noexcept {
    switch (kind) {
        case AreaKind::Outdoor:
            return "outdoor";
        case AreaKind::Greenhouse:
            return "greenhouse";
        default:
            return "unknown";
    }
}

Tilemap::Tilemap(int width, int height, int tileSize) {
    static_cast<void>(reset(width, height, tileSize));
}

core::Status Tilemap::reset(int width, int height, int tileSize) {
    if (width <= 0 || height <= 0 || width > kMaxDimension || height > kMaxDimension) {
        return core::errorStatus(tilemap_error::kInvalidDimensions, "tilemap dimensions must be between 1 and 4096");
    }
    if (tileSize <= 0 || tileSize > 1024) {
        return core::errorStatus(tilemap_error::kInvalidTileSize,
                                 "tilemap tile size must be between 1 and 1024 pixels");
    }

    m_width = width;
    m_height = height;
    m_tileSize = tileSize;
    m_origin = core::Vec2{0.0f, 0.0f};
    m_outsideIsSolid = true;
    m_tileDefs.clear();
    m_layers.clear();
    m_areas.clear();
    m_portals.clear();
    m_ambient = MapAmbient{};
    return core::okStatus();
}

std::size_t Tilemap::cellCount() const noexcept {
    if (m_width <= 0 || m_height <= 0) {
        return 0;
    }
    return static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height);
}

bool Tilemap::addTileDef(TileDef def) {
    if (!def.id.isValid() || defFor(def.id) != nullptr) {
        return false;
    }
    m_tileDefs.push_back(std::move(def));
    return true;
}

bool Tilemap::setTileTexture(TileId id, render::TextureHandle texture, core::Rect source) {
    for (TileDef& def : m_tileDefs) {
        if (def.id == id) {
            def.texture = texture;
            def.source = source;
            return true;
        }
    }
    return false;
}

const TileDef* Tilemap::tileDef(TileId id) const noexcept {
    return defFor(id);
}

TileDef* Tilemap::mutableTileDef(TileId id) noexcept {
    if (!id.isValid()) {
        return nullptr;
    }
    for (TileDef& def : m_tileDefs) {
        if (def.id == id) {
            return &def;
        }
    }
    return nullptr;
}

const TileDef* Tilemap::defFor(TileId id) const noexcept {
    if (!id.isValid()) {
        return nullptr;
    }
    for (const TileDef& def : m_tileDefs) {
        if (def.id == id) {
            return &def;
        }
    }
    return nullptr;
}

int Tilemap::addLayer(TileLayer layer) {
    if (layer.name.empty() || layer.renderLayer == render::RenderLayer::Count) {
        return -1;
    }
    if (layer.tiles.size() != cellCount()) {
        return -1;
    }
    if (findLayer(layer.name) >= 0) {
        return -1;
    }
    m_layers.push_back(std::move(layer));
    return static_cast<int>(m_layers.size()) - 1;
}

int Tilemap::findLayer(std::string_view name) const noexcept {
    for (std::size_t index = 0; index < m_layers.size(); ++index) {
        if (m_layers[index].name == name) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

TileLayer* Tilemap::layerAt(int index) noexcept {
    if (index < 0 || index >= layerCount()) {
        return nullptr;
    }
    return &m_layers[static_cast<std::size_t>(index)];
}

const TileLayer* Tilemap::layerAt(int index) const noexcept {
    if (index < 0 || index >= layerCount()) {
        return nullptr;
    }
    return &m_layers[static_cast<std::size_t>(index)];
}

bool Tilemap::contains(int column, int row) const noexcept {
    return column >= 0 && row >= 0 && column < m_width && row < m_height;
}

TileId Tilemap::tileAt(int column, int row, int layerIndex) const noexcept {
    if (!contains(column, row)) {
        return kEmptyTile;
    }
    const TileLayer* layer = layerAt(layerIndex);
    if (layer == nullptr) {
        return kEmptyTile;
    }
    const std::size_t index =
        static_cast<std::size_t>(row) * static_cast<std::size_t>(m_width) + static_cast<std::size_t>(column);
    return layer->tiles[index];
}

bool Tilemap::setTile(int column, int row, int layerIndex, TileId id) noexcept {
    if (!contains(column, row)) {
        return false;
    }
    TileLayer* layer = layerAt(layerIndex);
    if (layer == nullptr) {
        return false;
    }
    const std::size_t index =
        static_cast<std::size_t>(row) * static_cast<std::size_t>(m_width) + static_cast<std::size_t>(column);
    layer->tiles[index] = id;
    return true;
}

bool Tilemap::fillLayer(int layerIndex, TileId id) noexcept {
    TileLayer* layer = layerAt(layerIndex);
    if (layer == nullptr) {
        return false;
    }
    for (TileId& tile : layer->tiles) {
        tile = id;
    }
    return true;
}

bool Tilemap::setBorder(int layerIndex, TileId id) noexcept {
    TileLayer* layer = layerAt(layerIndex);
    if (layer == nullptr) {
        return false;
    }
    const auto at = [this, layer](int column, int row) -> TileId& {
        const std::size_t index =
            static_cast<std::size_t>(row) * static_cast<std::size_t>(m_width) + static_cast<std::size_t>(column);
        return layer->tiles[index];
    };
    for (int column = 0; column < m_width; ++column) {
        at(column, 0) = id;
        if (m_height > 1) {
            at(column, m_height - 1) = id;
        }
    }
    for (int row = 1; row + 1 < m_height; ++row) {
        at(0, row) = id;
        if (m_width > 1) {
            at(m_width - 1, row) = id;
        }
    }
    return true;
}

bool Tilemap::anyDefHas(int column, int row, bool TileDef::*member) const noexcept {
    if (!contains(column, row)) {
        return false;
    }
    const std::size_t cell =
        static_cast<std::size_t>(row) * static_cast<std::size_t>(m_width) + static_cast<std::size_t>(column);
    for (const TileLayer& layer : m_layers) {
        const TileDef* def = defFor(layer.tiles[cell]);
        if (def != nullptr && def->*member) {
            return true;
        }
    }
    return false;
}

bool Tilemap::isWalkable(int column, int row) const noexcept {
    if (!contains(column, row)) {
        return !m_outsideIsSolid;
    }
    if (isSolidCell(column, row)) {
        return false;
    }
    const std::size_t cell =
        static_cast<std::size_t>(row) * static_cast<std::size_t>(m_width) + static_cast<std::size_t>(column);
    for (const TileLayer& layer : m_layers) {
        const TileDef* def = defFor(layer.tiles[cell]);
        if (def != nullptr && !def->walkable) {
            return false;
        }
    }
    return true;
}

bool Tilemap::isWater(int column, int row) const noexcept {
    return anyDefHas(column, row, &TileDef::water);
}

bool Tilemap::isTillable(int column, int row) const noexcept {
    return anyDefHas(column, row, &TileDef::tillable);
}

bool Tilemap::isSolidCell(int column, int row) const noexcept {
    if (!contains(column, row)) {
        return m_outsideIsSolid;
    }
    return anyDefHas(column, row, &TileDef::solid);
}

TileCoord Tilemap::worldToGrid(core::Vec2 world) const noexcept {
    if (m_tileSize <= 0) {
        return TileCoord{};
    }
    const float inverse = 1.0f / static_cast<float>(m_tileSize);
    return TileCoord{clampToGrid((world.x - m_origin.x) * inverse), clampToGrid((world.y - m_origin.y) * inverse)};
}

core::Vec2 Tilemap::gridToWorld(TileCoord coord) const noexcept {
    return core::Vec2{m_origin.x + static_cast<float>(coord.x) * static_cast<float>(m_tileSize),
                      m_origin.y + static_cast<float>(coord.y) * static_cast<float>(m_tileSize)};
}

core::Rect Tilemap::tileRect(TileCoord coord) const noexcept {
    const core::Vec2 topLeft = gridToWorld(coord);
    return core::makeRect(topLeft.x, topLeft.y, static_cast<float>(m_tileSize), static_cast<float>(m_tileSize));
}

core::Rect Tilemap::boundsRect() const noexcept {
    return core::makeRect(m_origin.x,
                          m_origin.y,
                          static_cast<float>(m_width) * static_cast<float>(m_tileSize),
                          static_cast<float>(m_height) * static_cast<float>(m_tileSize));
}

void Tilemap::addArea(MapArea area) {
    m_areas.push_back(std::move(area));
}

std::optional<AreaKind> Tilemap::queryArea(TileCoord coord) const noexcept {
    const MapArea* area = areaAt(coord);
    if (area == nullptr) {
        return std::nullopt;
    }
    return area->kind;
}

const MapArea* Tilemap::areaAt(TileCoord coord) const noexcept {
    for (const MapArea& area : m_areas) {
        if (area.bounds.contains(coord)) {
            return &area;
        }
    }
    return nullptr;
}

void Tilemap::addPortal(MapPortal portal) {
    m_portals.push_back(std::move(portal));
}

const MapPortal* Tilemap::portalAt(TileCoord coord) const noexcept {
    for (const MapPortal& portal : m_portals) {
        if (portal.bounds.contains(coord)) {
            return &portal;
        }
    }
    return nullptr;
}

TilemapStats Tilemap::stats() const noexcept {
    TilemapStats stats;
    stats.width = m_width;
    stats.height = m_height;
    stats.tileSize = m_tileSize;
    stats.layers = m_layers.size();
    stats.tileDefs = m_tileDefs.size();
    stats.areas = m_areas.size();
    stats.portals = m_portals.size();

    const std::size_t cells = cellCount();
    for (std::size_t cell = 0; cell < cells; ++cell) {
        bool nonEmpty = false;
        bool solid = false;
        bool tillable = false;
        bool water = false;
        for (const TileLayer& layer : m_layers) {
            const TileDef* def = defFor(layer.tiles[cell]);
            if (def == nullptr) {
                continue;
            }
            nonEmpty = true;
            solid = solid || def->solid;
            tillable = tillable || def->tillable;
            water = water || def->water;
        }
        if (nonEmpty) {
            ++stats.nonEmptyTiles;
        }
        if (solid) {
            ++stats.solidTiles;
        }
        if (tillable) {
            ++stats.tillableTiles;
        }
        if (water) {
            ++stats.waterTiles;
        }
    }
    return stats;
}

float TilemapSolids::tileSize() const noexcept {
    return m_map != nullptr ? static_cast<float>(m_map->tileSize()) : static_cast<float>(render::kTileSize);
}

bool TilemapSolids::isSolid(int column, int row) const noexcept {
    return m_map != nullptr ? m_map->isSolidCell(column, row) : true;
}

int TilemapSolids::columns() const noexcept {
    return m_map != nullptr ? m_map->width() : 0;
}

int TilemapSolids::rows() const noexcept {
    return m_map != nullptr ? m_map->height() : 0;
}

}  // namespace fa::tilemap
