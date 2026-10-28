#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "fa/tilemap/TileId.hpp"

namespace fa::tilemap {

class Tilemap;

/// \file
/// Minimal 4-neighbour autotiling.
///
/// v1 bakes variants at load/edit time instead of resolving them every frame: the
/// map already stores one `TileId` per cell, so a variant lookup in the draw path
/// would cost more than the sixteen entries it saves. The mask is a plain bit field,
/// which is the format later rule sets (47-variant or Wang) can reuse.

/// \brief Bit positions of the four orthogonal neighbours.
enum class NeighborBit : std::uint8_t {
    North = 1u << 0,
    East = 1u << 1,
    South = 1u << 2,
    West = 1u << 3,
};

/// \brief Four-bit neighbour mask; bit N means "the neighbour is the base tile".
using NeighborMask = std::uint8_t;

/// \brief Number of masks a 4-neighbour rule can see.
inline constexpr std::size_t kAutotileVariantCount = 16;

/// \brief Builds the mask for one cell by comparing its four neighbours to `match`.
///
/// Cells outside the map count as matches. That is what gives a finite map coherent
/// edge variants instead of a one-cell-wide seam of "interior" tiles.
[[nodiscard]] NeighborMask neighborMask(const Tilemap& map, int column, int row, int layerIndex, TileId match) noexcept;

/// \brief Maps a neighbour mask to a variant tile.
///
/// An empty variant entry falls back to `base`, so a rule may declare only the
/// variants it actually differentiates.
struct AutotileRule {
    TileId base{};
    std::array<TileId, kAutotileVariantCount> variants{};

    [[nodiscard]] TileId variantFor(NeighborMask mask) const noexcept;
};

/// \brief Rewrites every `rule.base` cell of one layer to its variant.
///
/// Neighbour masks are computed against a snapshot of the layer, so the order of
/// writes cannot change which variants later cells receive.
///
/// \return Number of cells whose tile id changed.
[[nodiscard]] std::size_t bakeAutotiles(Tilemap& map, int layerIndex, const AutotileRule& rule);

}  // namespace fa::tilemap
