#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "fa/core/Math.hpp"

namespace fa::physics {

/// \file
/// Static collision for grid maps.
///
/// Physics does not own the tile map: M12 will own the data, and this module only needs
/// to ask "is this cell solid". That question is a one-method interface, so a test can
/// supply a two-by-two grid, M12 can supply its real map, and `fa_physics` never links
/// the tilemap module.

/// \brief Read-only view of which grid cells block movement.
class ISolidTiles {
public:
    ISolidTiles() = default;
    virtual ~ISolidTiles() = default;
    ISolidTiles(const ISolidTiles&) = delete;
    ISolidTiles& operator=(const ISolidTiles&) = delete;
    ISolidTiles(ISolidTiles&&) = delete;
    ISolidTiles& operator=(ISolidTiles&&) = delete;

    /// \brief Edge length of one cell in world pixels.
    [[nodiscard]] virtual float tileSize() const noexcept = 0;

    /// \brief Whether the cell at `column`/`row` blocks movement.
    ///
    /// Coordinates outside the map are the provider's decision: returning true there
    /// is what gives a finite map solid outer walls.
    [[nodiscard]] virtual bool isSolid(int column, int row) const noexcept = 0;

    /// \brief Column count, or 0 when the map has no finite extent.
    ///
    /// Only used by the debug overlay to iterate the grid; physics never needs it.
    [[nodiscard]] virtual int columns() const noexcept { return 0; }

    /// \brief Row count, or 0 when the map has no finite extent.
    [[nodiscard]] virtual int rows() const noexcept { return 0; }
};

/// \brief A plain solid/empty grid.
///
/// Small, serialisable and dependency-free, which is what makes it usable in tests and
/// in the smoke app long before the real tile map exists.
class TileGrid final : public ISolidTiles {
public:
    TileGrid() = default;
    /// \brief Builds an all-empty grid. A non-positive tile size falls back to 16.
    TileGrid(int columns, int rows, float tileSize);

    /// \brief Resizes to an all-empty grid and resets `outsideIsSolid` to true.
    void resize(int columns, int rows, float tileSize);

    [[nodiscard]] float tileSize() const noexcept override { return m_tileSize; }
    [[nodiscard]] int columns() const noexcept override { return m_columns; }
    [[nodiscard]] int rows() const noexcept override { return m_rows; }

    /// \brief Solid flag of one cell; out-of-range cells follow `outsideIsSolid()`.
    [[nodiscard]] bool isSolid(int column, int row) const noexcept override;

    /// \return false when the cell is outside the grid, in which case nothing changed.
    bool setSolid(int column, int row, bool solid);

    /// \brief Marks the outermost ring of cells solid (or empty) in one call.
    void setBorderSolid(bool solid);

    /// \brief Whether cells outside the grid block movement. Default true.
    [[nodiscard]] bool outsideIsSolid() const noexcept { return m_outsideIsSolid; }
    void setOutsideIsSolid(bool solid) noexcept { m_outsideIsSolid = solid; }

    /// \brief Number of solid cells inside the grid.
    [[nodiscard]] std::size_t solidCount() const noexcept { return m_solidCount; }

    /// \brief Marks every cell empty, keeping the extent.
    void clearSolid() noexcept;

private:
    [[nodiscard]] std::size_t indexOf(int column, int row) const noexcept;

    int m_columns = 0;
    int m_rows = 0;
    float m_tileSize = 16.0f;
    std::vector<std::uint8_t> m_solid;
    std::size_t m_solidCount = 0;
    bool m_outsideIsSolid = true;
};

/// \brief Whether any solid cell overlaps `area`.
///
/// Uses the same half-open rule as `core::rectOverlaps`: a box whose right edge lands
/// exactly on a grid line does not touch the cell to its right, so a body can rest
/// flush against a wall without being reported as inside it.
[[nodiscard]] bool overlapsSolidTile(const ISolidTiles& tiles, const core::Rect& area) noexcept;

}  // namespace fa::physics
