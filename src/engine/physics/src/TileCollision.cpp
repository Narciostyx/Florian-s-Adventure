#include "fa/physics/TileCollision.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace fa::physics {

namespace {

constexpr float kDefaultTileSize = 16.0f;

[[nodiscard]] float sanitizeTileSize(float tileSize) noexcept {
    return tileSize > 0.0f ? tileSize : kDefaultTileSize;
}

/// \brief Index of the cell containing `value`.
[[nodiscard]] int firstCell(float value, float inverseTile) noexcept {
    return static_cast<int>(std::floor(value * inverseTile));
}

/// \brief Index of the last cell the half-open span `[value, value + extent)` reaches.
[[nodiscard]] int lastCell(float value, float extent, float inverseTile) noexcept {
    const int first = firstCell(value, inverseTile);
    const int last = static_cast<int>(std::ceil((value + extent) * inverseTile)) - 1;
    return last < first ? first : last;
}

}  // namespace

TileGrid::TileGrid(int columns, int rows, float tileSize) {
    resize(columns, rows, tileSize);
}

void TileGrid::resize(int columns, int rows, float tileSize) {
    m_columns = columns > 0 ? columns : 0;
    m_rows = rows > 0 ? rows : 0;
    m_tileSize = sanitizeTileSize(tileSize);
    m_solid.assign(static_cast<std::size_t>(m_columns) * static_cast<std::size_t>(m_rows), std::uint8_t{0});
    m_solidCount = 0;
    m_outsideIsSolid = true;
}

std::size_t TileGrid::indexOf(int column, int row) const noexcept {
    return static_cast<std::size_t>(row) * static_cast<std::size_t>(m_columns) + static_cast<std::size_t>(column);
}

bool TileGrid::isSolid(int column, int row) const noexcept {
    if (column < 0 || row < 0 || column >= m_columns || row >= m_rows) {
        return m_outsideIsSolid;
    }
    return m_solid[indexOf(column, row)] != 0u;
}

bool TileGrid::setSolid(int column, int row, bool solid) {
    if (column < 0 || row < 0 || column >= m_columns || row >= m_rows) {
        return false;
    }
    const std::size_t index = indexOf(column, row);
    const bool wasSolid = m_solid[index] != 0u;
    if (wasSolid == solid) {
        return true;
    }
    m_solid[index] = solid ? std::uint8_t{1} : std::uint8_t{0};
    if (solid) {
        ++m_solidCount;
    } else {
        --m_solidCount;
    }
    return true;
}

void TileGrid::setBorderSolid(bool solid) {
    if (m_columns <= 0 || m_rows <= 0) {
        return;
    }
    for (int column = 0; column < m_columns; ++column) {
        static_cast<void>(setSolid(column, 0, solid));
        static_cast<void>(setSolid(column, m_rows - 1, solid));
    }
    for (int row = 1; row + 1 < m_rows; ++row) {
        static_cast<void>(setSolid(0, row, solid));
        static_cast<void>(setSolid(m_columns - 1, row, solid));
    }
}

void TileGrid::clearSolid() noexcept {
    std::fill(m_solid.begin(), m_solid.end(), std::uint8_t{0});
    m_solidCount = 0;
}

bool overlapsSolidTile(const ISolidTiles& tiles, const core::Rect& area) noexcept {
    if (core::isEmpty(area)) {
        return false;
    }
    const float tileSize = tiles.tileSize();
    if (tileSize <= 0.0f) {
        return false;
    }
    const float inverse = 1.0f / tileSize;
    const int minColumn = firstCell(area.x, inverse);
    const int maxColumn = lastCell(area.x, area.w, inverse);
    const int minRow = firstCell(area.y, inverse);
    const int maxRow = lastCell(area.y, area.h, inverse);
    for (int row = minRow; row <= maxRow; ++row) {
        for (int column = minColumn; column <= maxColumn; ++column) {
            if (tiles.isSolid(column, row)) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace fa::physics
