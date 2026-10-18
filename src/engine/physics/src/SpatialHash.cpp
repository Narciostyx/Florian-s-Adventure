#include "fa/physics/SpatialHash.hpp"

#include <algorithm>
#include <cmath>

namespace fa::physics {

namespace {

constexpr float kDefaultCellSize = 64.0f;

[[nodiscard]] float sanitizeCellSize(float cellSize) noexcept {
    return cellSize > 0.0f ? cellSize : kDefaultCellSize;
}

}  // namespace

SpatialHash::SpatialHash(float cellSize) : m_cellSize(sanitizeCellSize(cellSize)) {}

void SpatialHash::setCellSize(float cellSize) noexcept {
    m_cellSize = sanitizeCellSize(cellSize);
    clear();
}

void SpatialHash::clear() noexcept {
    m_cells.clear();
    m_entryCount = 0;
    m_maxOccupancy = 0;
    ++m_rebuilds;
}

SpatialHash::CellRange SpatialHash::rangeOf(const core::Rect& area) const noexcept {
    const float inverse = 1.0f / m_cellSize;
    const int minX = static_cast<int>(std::floor(area.x * inverse));
    const int minY = static_cast<int>(std::floor(area.y * inverse));
    // `x + w` is exclusive, so a box ending exactly on a grid line must not pull in the
    // cell beyond it. Otherwise a body resting flush against a wall would keep testing
    // that wall for as long as it stands there.
    int maxX = static_cast<int>(std::ceil(core::right(area) * inverse)) - 1;
    int maxY = static_cast<int>(std::ceil(core::bottom(area) * inverse)) - 1;
    if (maxX < minX) {
        maxX = minX;
    }
    if (maxY < minY) {
        maxY = minY;
    }
    return CellRange{minX, minY, maxX, maxY};
}

std::uint64_t SpatialHash::keyOf(int x, int y) noexcept {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) |
           static_cast<std::uint64_t>(static_cast<std::uint32_t>(y));
}

void SpatialHash::insert(scene::EntityId id, const core::Rect& bounds) {
    const CellRange range = rangeOf(bounds);
    for (int y = range.minY; y <= range.maxY; ++y) {
        for (int x = range.minX; x <= range.maxX; ++x) {
            std::vector<scene::EntityId>& cell = m_cells[keyOf(x, y)];
            cell.push_back(id);
            ++m_entryCount;
            if (cell.size() > m_maxOccupancy) {
                m_maxOccupancy = cell.size();
            }
        }
    }
}

void SpatialHash::query(const core::Rect& area, std::vector<scene::EntityId>& out) const {
    ++m_queries;
    const CellRange range = rangeOf(area);
    for (int y = range.minY; y <= range.maxY; ++y) {
        for (int x = range.minX; x <= range.maxX; ++x) {
            const auto found = m_cells.find(keyOf(x, y));
            if (found == m_cells.end()) {
                continue;
            }
            out.insert(out.end(), found->second.begin(), found->second.end());
        }
    }
}

std::vector<scene::EntityId> SpatialHash::queryUnique(const core::Rect& area) const {
    std::vector<scene::EntityId> ids;
    query(area, ids);
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

}  // namespace fa::physics
