#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "fa/core/Math.hpp"
#include "fa/core/Result.hpp"
#include "fa/scene/EntityId.hpp"
#include "fa/tilemap/TileId.hpp"

namespace fa::tilemap {
class Tilemap;
}

namespace fa::interaction {

/// \file
/// Occupancy for placed objects, kept separate from tile collision.
///
/// A placed chest or sprinkler may sit on a walkable grass tile while still making
/// the cell unavailable to another placement. M17 owns this grid; M19 owns the
/// gameplay entity. That split keeps placement validation testable and stops M17
/// from growing a crop/furniture model.

/// \brief Grid of cells claimed by placed objects.
///
/// The grid is deliberately independent of `Tilemap`: it stores only who owns a cell
/// and an opaque payload. `canPlace()` reads the map for walkability and bounds, so
/// the two authorities stay explicit.
class PlacementGrid {
public:
    /// \brief Largest accepted width or height, matching `Tilemap::kMaxDimension`.
    static constexpr int kMaxDimension = 4096;

    PlacementGrid() = default;
    explicit PlacementGrid(int columns, int rows);

    /// \brief Rebuilds an empty grid. Invalid sizes leave the old grid untouched.
    [[nodiscard]] core::Status resize(int columns, int rows);

    [[nodiscard]] int columns() const noexcept { return m_columns; }
    [[nodiscard]] int rows() const noexcept { return m_rows; }
    [[nodiscard]] bool contains(int column, int row) const noexcept;

    [[nodiscard]] bool isOccupied(int column, int row) const noexcept;
    [[nodiscard]] scene::EntityId ownerAt(int column, int row) const noexcept;
    [[nodiscard]] std::uint32_t payloadAt(int column, int row) const noexcept;

    /// \brief Cells a world-space edge covers, rounded up. 0 for a non-positive size.
    [[nodiscard]] static int cellsAlong(float size, int tileSize) noexcept;

    /// \brief World-space rectangle covering every footprint cell.
    ///
    /// An invalid size returns an empty rect. The rectangle may extend beyond the map
    /// when the origin is out of bounds; callers use `canPlace()` to reject that.
    [[nodiscard]] static core::Rect
    footprintRect(const tilemap::Tilemap& map, tilemap::TileCoord origin, core::Vec2 size) noexcept;

    /// \brief Whether the footprint is in bounds, walkable and unoccupied.
    [[nodiscard]] bool canPlace(const tilemap::Tilemap& map, tilemap::TileCoord origin, core::Vec2 size) const noexcept;

    /// \brief Claims every footprint cell for `owner`.
    ///
    /// No partial writes: if any cell is unusable, nothing changes.
    /// \return false for an invalid owner, invalid size, or an occupied/blocked cell.
    bool place(const tilemap::Tilemap& map,
               tilemap::TileCoord origin,
               core::Vec2 size,
               scene::EntityId owner,
               std::uint32_t payload = 0) noexcept;

    /// \brief Clears every cell claimed by `owner`. \return number of cells cleared.
    std::size_t remove(scene::EntityId owner) noexcept;

    /// \brief Clears all occupancy, keeping the dimensions.
    void clear() noexcept;

    [[nodiscard]] std::size_t occupiedCells() const noexcept { return m_occupied; }

private:
    struct Cell {
        scene::EntityId owner{};
        std::uint32_t payload = 0;
    };

    [[nodiscard]] std::size_t indexOf(int column, int row) const noexcept;

    int m_columns = 0;
    int m_rows = 0;
    std::vector<Cell> m_cells;
    std::size_t m_occupied = 0;
};

}  // namespace fa::interaction
