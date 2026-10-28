#pragma once

#include <cstdint>

namespace fa::tilemap {

/// \file
/// Small value types shared by every tilemap interface: the tile id, a grid
/// coordinate and a half-open tile rectangle.
///
/// They are deliberately dependency-free so a content validator, a save file and a
/// gameplay system can all refer to a cell without linking the map itself.

/// \brief Identifier of one tile kind.
///
/// Zero is the empty cell. Every definition starts at 1, which makes "no tile" and
/// "tile id 0" the same state and removes a second validity flag from every layer.
struct TileId {
    std::uint16_t value = 0;

    constexpr TileId() noexcept = default;
    explicit constexpr TileId(std::uint16_t id) noexcept : value(id) {}

    [[nodiscard]] constexpr bool isValid() const noexcept { return value != 0; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return isValid(); }

    friend constexpr bool operator==(TileId lhs, TileId rhs) noexcept = default;
};

/// \brief The empty cell.
inline constexpr TileId kEmptyTile{};

/// \brief Largest representable tile id.
inline constexpr std::uint16_t kMaxTileId = 65535;

/// \brief Integer grid coordinate, origin at the map's top-left cell.
struct TileCoord {
    int x = 0;
    int y = 0;

    friend constexpr bool operator==(TileCoord lhs, TileCoord rhs) noexcept = default;
};

/// \brief Half-open tile rectangle: `x .. x + width - 1`, `y .. y + height - 1`.
struct TileBounds {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    [[nodiscard]] constexpr bool isEmpty() const noexcept { return width <= 0 || height <= 0; }

    [[nodiscard]] constexpr bool contains(TileCoord coord) const noexcept {
        return coord.x >= x && coord.y >= y && coord.x < x + width && coord.y < y + height;
    }

    friend constexpr bool operator==(TileBounds lhs, TileBounds rhs) noexcept = default;
};

}  // namespace fa::tilemap
