#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "fa/core/Math.hpp"
#include "fa/scene/EntityId.hpp"

namespace fa::physics {

/// \file
/// Uniform-grid broadphase.
///
/// Every query in `PhysicsWorld` starts here: instead of testing one box against every
/// body, the grid narrows the candidates to the cells the query box covers. It stores
/// ids only, never pointers, so a rebuild after structural changes is always safe.
///
/// The grid is rebuilt from scratch (`clear` + `insert`) rather than incrementally
/// updated. At the entity counts this game targets (hundreds) a rebuild is cheaper
/// than maintaining per-entity cell lists, and it cannot go stale.

/// \brief Hash grid over integer cells of a fixed edge length.
class SpatialHash {
public:
    /// \brief Inclusive cell range a rectangle covers.
    struct CellRange {
        int minX = 0;
        int minY = 0;
        int maxX = 0;
        int maxY = 0;
    };

    /// \brief Sets the cell edge length; a non-positive value falls back to 64.
    explicit SpatialHash(float cellSize = 64.0f);

    /// \brief Changes the cell size and drops every entry.
    void setCellSize(float cellSize) noexcept;

    [[nodiscard]] float cellSize() const noexcept { return m_cellSize; }

    /// \brief Drops every entry. Counts as one rebuild.
    void clear() noexcept;

    /// \brief Inserts `id` into every cell `bounds` covers.
    void insert(scene::EntityId id, const core::Rect& bounds);

    /// \brief Appends the ids in the cells `area` covers. Duplicates are possible.
    void query(const core::Rect& area, std::vector<scene::EntityId>& out) const;

    /// \brief Sorted, duplicate-free ids in `area`.
    ///
    /// Sorted so callers get a deterministic visit order: two runs over the same world
    /// must produce the same collision order, or fixed-step replay stops matching.
    [[nodiscard]] std::vector<scene::EntityId> queryUnique(const core::Rect& area) const;

    /// \brief Cells currently owning at least one entry.
    [[nodiscard]] std::size_t cellCount() const noexcept { return m_cells.size(); }
    /// \brief Total cell entries, i.e. insertions across all cells.
    [[nodiscard]] std::size_t entryCount() const noexcept { return m_entryCount; }
    /// \brief Largest number of ids in one cell, the hash's worst-case bucket.
    [[nodiscard]] std::size_t maxCellOccupancy() const noexcept { return m_maxOccupancy; }
    /// \brief Times the grid was cleared and rebuilt.
    [[nodiscard]] std::size_t rebuilds() const noexcept { return m_rebuilds; }
    /// \brief Queries served since construction.
    [[nodiscard]] std::uint64_t queries() const noexcept { return m_queries; }

    /// \brief The cells `area` covers.
    ///
    /// A zero-size rectangle still maps to exactly one cell, so a point query works
    /// without a special case.
    [[nodiscard]] CellRange rangeOf(const core::Rect& area) const noexcept;

private:
    [[nodiscard]] static std::uint64_t keyOf(int x, int y) noexcept;

    std::unordered_map<std::uint64_t, std::vector<scene::EntityId>> m_cells;
    float m_cellSize = 64.0f;
    std::size_t m_entryCount = 0;
    std::size_t m_maxOccupancy = 0;
    std::size_t m_rebuilds = 0;
    /// Counter only, so a `const` query can still be counted.
    mutable std::uint64_t m_queries = 0;
};

}  // namespace fa::physics
