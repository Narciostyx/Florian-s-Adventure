#include "fa/interaction/PlacementGrid.hpp"

#include "fa/interaction/InteractionError.hpp"
#include "fa/tilemap/Tilemap.hpp"

#include <cmath>

namespace fa::interaction {

PlacementGrid::PlacementGrid(int columns, int rows) {
    static_cast<void>(resize(columns, rows));
}

core::Status PlacementGrid::resize(int columns, int rows) {
    if (columns <= 0 || rows <= 0 || columns > kMaxDimension || rows > kMaxDimension) {
        return core::errorStatus(interaction_error::kInvalidGrid,
                                 "placement grid dimensions must be between 1 and 4096");
    }
    m_columns = columns;
    m_rows = rows;
    m_cells.assign(static_cast<std::size_t>(columns) * static_cast<std::size_t>(rows), Cell{});
    m_occupied = 0;
    return core::okStatus();
}

bool PlacementGrid::contains(int column, int row) const noexcept {
    return column >= 0 && row >= 0 && column < m_columns && row < m_rows;
}

std::size_t PlacementGrid::indexOf(int column, int row) const noexcept {
    return static_cast<std::size_t>(row) * static_cast<std::size_t>(m_columns) + static_cast<std::size_t>(column);
}

bool PlacementGrid::isOccupied(int column, int row) const noexcept {
    if (!contains(column, row)) {
        return false;
    }
    return m_cells[indexOf(column, row)].owner.isValid();
}

scene::EntityId PlacementGrid::ownerAt(int column, int row) const noexcept {
    if (!contains(column, row)) {
        return scene::EntityId{};
    }
    return m_cells[indexOf(column, row)].owner;
}

std::uint32_t PlacementGrid::payloadAt(int column, int row) const noexcept {
    if (!contains(column, row)) {
        return 0;
    }
    return m_cells[indexOf(column, row)].payload;
}

int PlacementGrid::cellsAlong(float size, int tileSize) noexcept {
    if (!(size > 0.0f) || tileSize <= 0) {
        return 0;
    }
    const float cells = std::ceil(size / static_cast<float>(tileSize));
    if (cells > static_cast<float>(kMaxDimension)) {
        return kMaxDimension;
    }
    return static_cast<int>(cells);
}

core::Rect
PlacementGrid::footprintRect(const tilemap::Tilemap& map, tilemap::TileCoord origin, core::Vec2 size) noexcept {
    const int cellsX = cellsAlong(size.x, map.tileSize());
    const int cellsY = cellsAlong(size.y, map.tileSize());
    if (cellsX <= 0 || cellsY <= 0) {
        return core::Rect{};
    }
    const core::Vec2 topLeft = map.gridToWorld(origin);
    const float tile = static_cast<float>(map.tileSize());
    return core::makeRect(topLeft.x, topLeft.y, static_cast<float>(cellsX) * tile, static_cast<float>(cellsY) * tile);
}

bool PlacementGrid::canPlace(const tilemap::Tilemap& map, tilemap::TileCoord origin, core::Vec2 size) const noexcept {
    const int cellsX = cellsAlong(size.x, map.tileSize());
    const int cellsY = cellsAlong(size.y, map.tileSize());
    if (cellsX <= 0 || cellsY <= 0) {
        return false;
    }
    if (!contains(origin.x, origin.y)) {
        return false;
    }
    const long long right = static_cast<long long>(origin.x) + cellsX;
    const long long bottom = static_cast<long long>(origin.y) + cellsY;
    if (right > static_cast<long long>(map.width()) || bottom > static_cast<long long>(map.height()) ||
        right > static_cast<long long>(m_columns) || bottom > static_cast<long long>(m_rows)) {
        return false;
    }
    for (int row = origin.y; row < origin.y + cellsY; ++row) {
        for (int column = origin.x; column < origin.x + cellsX; ++column) {
            if (!map.isWalkable(column, row) || isOccupied(column, row)) {
                return false;
            }
        }
    }
    return true;
}

bool PlacementGrid::place(const tilemap::Tilemap& map,
                          tilemap::TileCoord origin,
                          core::Vec2 size,
                          scene::EntityId owner,
                          std::uint32_t payload) noexcept {
    if (!owner.isValid() || !canPlace(map, origin, size)) {
        return false;
    }
    const int cellsX = cellsAlong(size.x, map.tileSize());
    const int cellsY = cellsAlong(size.y, map.tileSize());
    for (int row = origin.y; row < origin.y + cellsY; ++row) {
        for (int column = origin.x; column < origin.x + cellsX; ++column) {
            Cell& cell = m_cells[indexOf(column, row)];
            cell.owner = owner;
            cell.payload = payload;
        }
    }
    m_occupied += static_cast<std::size_t>(cellsX) * static_cast<std::size_t>(cellsY);
    return true;
}

std::size_t PlacementGrid::remove(scene::EntityId owner) noexcept {
    if (!owner.isValid()) {
        return 0;
    }
    std::size_t removed = 0;
    for (Cell& cell : m_cells) {
        if (cell.owner == owner) {
            cell = Cell{};
            ++removed;
        }
    }
    m_occupied -= removed;
    return removed;
}

void PlacementGrid::clear() noexcept {
    for (Cell& cell : m_cells) {
        cell = Cell{};
    }
    m_occupied = 0;
}

}  // namespace fa::interaction
