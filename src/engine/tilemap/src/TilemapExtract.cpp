#include "fa/tilemap/TilemapExtract.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace fa::tilemap {

namespace {

/// \brief Index of the first cell a world coordinate reaches.
[[nodiscard]] int firstCell(float value, float origin, float tile, int limit) noexcept {
    if (tile <= 0.0f) {
        return 0;
    }
    const float raw = std::floor((value - origin) / tile);
    if (raw <= 0.0f) {
        return 0;
    }
    if (raw >= static_cast<float>(limit - 1)) {
        return limit - 1;
    }
    return static_cast<int>(raw);
}

/// \brief Index of the last cell a half-open world span reaches.
[[nodiscard]] int lastCell(float value, float origin, float tile, int limit) noexcept {
    if (tile <= 0.0f) {
        return limit - 1;
    }
    const float raw = std::ceil((value - origin) / tile) - 1.0f;
    if (raw <= 0.0f) {
        return 0;
    }
    if (raw >= static_cast<float>(limit - 1)) {
        return limit - 1;
    }
    return static_cast<int>(raw);
}

}  // namespace

TileExtractStats extractTiles(const Tilemap& map, render::RenderQueue& queue, const TileExtractOptions& options) {
    TileExtractStats stats;
    if (map.empty()) {
        return stats;
    }

    std::vector<int> order;
    order.reserve(static_cast<std::size_t>(map.layerCount()));
    for (int index = 0; index < map.layerCount(); ++index) {
        const TileLayer* layer = map.layerAt(index);
        if (layer != nullptr && layer->visible) {
            order.push_back(index);
        }
    }
    std::stable_sort(order.begin(), order.end(), [&map](int lhs, int rhs) {
        return map.layerAt(lhs)->zOrder < map.layerAt(rhs)->zOrder;
    });
    stats.layers = order.size();

    int minColumn = 0;
    int maxColumn = map.width() - 1;
    int minRow = 0;
    int maxRow = map.height() - 1;

    core::Rect visible = map.boundsRect();
    if (options.cullToViewBounds && !core::isEmpty(options.viewBounds)) {
        visible = core::intersect(visible, options.viewBounds);
        if (core::isEmpty(visible)) {
            stats.culled = order.size() * map.cellCount();
            return stats;
        }
        const float tile = static_cast<float>(map.tileSize());
        const core::Vec2 origin = map.origin();
        minColumn = firstCell(core::left(visible), origin.x, tile, map.width());
        maxColumn = lastCell(core::right(visible), origin.x, tile, map.width());
        minRow = firstCell(core::top(visible), origin.y, tile, map.height());
        maxRow = lastCell(core::bottom(visible), origin.y, tile, map.height());
    }

    const int columns = maxColumn - minColumn + 1;
    const int rows = maxRow - minRow + 1;
    const std::size_t visitedPerLayer = static_cast<std::size_t>(columns) * static_cast<std::size_t>(rows);
    const std::size_t totalCells = order.size() * map.cellCount();
    stats.culled = totalCells - visitedPerLayer * order.size();

    const int mapWidth = map.width();
    const core::Vec2 mapOrigin = map.origin();
    const float tile = static_cast<float>(map.tileSize());
    std::uint32_t nextEntityId = 0;

    for (const int layerIndex : order) {
        const TileLayer& layer = *map.layerAt(layerIndex);
        for (int row = minRow; row <= maxRow; ++row) {
            for (int column = minColumn; column <= maxColumn; ++column) {
                ++stats.candidates;
                const std::size_t cell = static_cast<std::size_t>(row) * static_cast<std::size_t>(mapWidth) +
                                         static_cast<std::size_t>(column);
                const TileId id = layer.tiles[cell];
                if (!id.isValid()) {
                    ++stats.skippedEmpty;
                    continue;
                }

                const TileDef* def = map.tileDef(id);
                if (def == nullptr || (!def->texture.isValid() && options.skipWithoutTexture)) {
                    ++stats.skippedNoTexture;
                    continue;
                }

                render::DrawItem item;
                item.texture = def->texture;
                item.position = core::Vec2{mapOrigin.x + static_cast<float>(column) * tile,
                                           mapOrigin.y + static_cast<float>(row) * tile};
                item.size = core::Vec2{tile, tile};
                item.source = def->source;
                item.layer = layer.renderLayer;
                item.sortY = item.position.y + tile;
                item.entityId = ++nextEntityId;
                queue.submit(item);
                ++stats.submitted;
            }
        }
    }
    return stats;
}

TilemapExtractSystem::TilemapExtractSystem(const Tilemap& map, TileExtractOptions options)
    : m_map(&map), m_options(options) {}

std::string_view TilemapExtractSystem::name() const noexcept {
    return "tilemap.extract";
}

void TilemapExtractSystem::onRenderExtract(const scene::World& world, render::RenderQueue& queue) {
    static_cast<void>(world);
    if (m_map != nullptr) {
        m_lastStats = extractTiles(*m_map, queue, m_options);
    } else {
        m_lastStats = TileExtractStats{};
    }
}

void TilemapExtractSystem::setViewBounds(core::Rect bounds) noexcept {
    m_options.viewBounds = bounds;
    m_options.cullToViewBounds = !core::isEmpty(bounds);
}

void TilemapExtractSystem::clearViewBounds() noexcept {
    m_options.viewBounds = core::Rect{};
    m_options.cullToViewBounds = false;
}

}  // namespace fa::tilemap
