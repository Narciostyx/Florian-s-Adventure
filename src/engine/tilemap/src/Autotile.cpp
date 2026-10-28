#include "fa/tilemap/Autotile.hpp"

#include "fa/tilemap/Tilemap.hpp"

#include <vector>

namespace fa::tilemap {

NeighborMask neighborMask(const Tilemap& map, int column, int row, int layerIndex, TileId match) noexcept {
    if (!match.isValid()) {
        return 0;
    }
    const auto matches = [&map, layerIndex, match](int x, int y) -> bool {
        if (!map.contains(x, y)) {
            return true;
        }
        return map.tileAt(x, y, layerIndex) == match;
    };

    NeighborMask mask = 0;
    if (matches(column, row - 1)) {
        mask |= static_cast<NeighborMask>(NeighborBit::North);
    }
    if (matches(column + 1, row)) {
        mask |= static_cast<NeighborMask>(NeighborBit::East);
    }
    if (matches(column, row + 1)) {
        mask |= static_cast<NeighborMask>(NeighborBit::South);
    }
    if (matches(column - 1, row)) {
        mask |= static_cast<NeighborMask>(NeighborBit::West);
    }
    return mask;
}

TileId AutotileRule::variantFor(NeighborMask mask) const noexcept {
    if (mask >= kAutotileVariantCount) {
        return base;
    }
    const TileId variant = variants[mask];
    return variant.isValid() ? variant : base;
}

std::size_t bakeAutotiles(Tilemap& map, int layerIndex, const AutotileRule& rule) {
    if (!rule.base.isValid()) {
        return 0;
    }
    const TileLayer* layer = map.layerAt(layerIndex);
    if (layer == nullptr) {
        return 0;
    }

    const std::vector<TileId> original = layer->tiles;
    const int width = map.width();
    const int height = map.height();
    const auto originalAt = [&original, width, height, &rule](int x, int y) -> TileId {
        if (x < 0 || y < 0 || x >= width || y >= height) {
            return rule.base;
        }
        const std::size_t index =
            static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
        return original[index];
    };

    std::size_t changed = 0;
    for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width; ++column) {
            if (originalAt(column, row) != rule.base) {
                continue;
            }
            NeighborMask mask = 0;
            if (originalAt(column, row - 1) == rule.base) {
                mask |= static_cast<NeighborMask>(NeighborBit::North);
            }
            if (originalAt(column + 1, row) == rule.base) {
                mask |= static_cast<NeighborMask>(NeighborBit::East);
            }
            if (originalAt(column, row + 1) == rule.base) {
                mask |= static_cast<NeighborMask>(NeighborBit::South);
            }
            if (originalAt(column - 1, row) == rule.base) {
                mask |= static_cast<NeighborMask>(NeighborBit::West);
            }

            const TileId variant = rule.variantFor(mask);
            if (variant != rule.base) {
                static_cast<void>(map.setTile(column, row, layerIndex, variant));
                ++changed;
            }
        }
    }
    return changed;
}

}  // namespace fa::tilemap
