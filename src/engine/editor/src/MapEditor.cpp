#include "fa/editor/MapEditor.hpp"

#include <algorithm>
#include <utility>

#include "fa/tilemap/Tilemap.hpp"
#include "fa/tilemap/TilemapWriter.hpp"

namespace fa::editor {
namespace {

constexpr std::size_t kMaxEditedCells = 4096;

}  // namespace

MapEditor::MapEditor(tilemap::Tilemap& map, EditorHistory& history) : m_map(map), m_history(history) {}

void MapEditor::setLayer(int layer) noexcept {
    const int count = m_map.layerCount();
    if (count <= 0) {
        m_layer = 0;
        return;
    }
    m_layer = std::clamp(layer, 0, count - 1);
}

core::Status MapEditor::applyPrimary() {
    if (!m_map.contains(m_hover.x, m_hover.y)) {
        return core::errorStatus(core::error_code::kOutOfRange, "editor hover is outside the map");
    }
    switch (m_tool) {
        case EditorTool::Brush:
            return paint(m_hover, m_brush);
        case EditorTool::Erase:
            return erase(m_hover);
        case EditorTool::Fill:
            return floodFill(m_hover, m_brush);
        case EditorTool::Picker:
            m_brush = m_map.tileAt(m_hover.x, m_hover.y, m_layer);
            return core::okStatus();
        case EditorTool::Rect: {
            if (!m_rectActive) {
                m_rectStart = m_hover;
                m_rectActive = true;
                return core::okStatus();
            }
            const core::Status status = fillRect(m_rectStart, m_hover, m_brush);
            m_rectActive = false;
            return status;
        }
        default:
            return core::errorStatus(core::error_code::kUnsupported, "unknown editor tool");
    }
}

core::Status MapEditor::applySecondary() {
    if (m_rectActive) {
        m_rectActive = false;
        return core::okStatus();
    }
    return erase(m_hover);
}

core::Status MapEditor::paint(tilemap::TileCoord cell, tilemap::TileId tile) {
    if (!m_map.contains(cell.x, cell.y)) {
        return core::errorStatus(core::error_code::kOutOfRange, "paint cell is outside the map");
    }
    const tilemap::TileId before = m_map.tileAt(cell.x, cell.y, m_layer);
    if (before == tile) {
        return core::okStatus();
    }
    TileChange change;
    change.cell = cell;
    change.layer = m_layer;
    change.before = before;
    change.after = tile;
    return applyChanges({std::move(change)}, "paint");
}

core::Status MapEditor::erase(tilemap::TileCoord cell) {
    return paint(cell, tilemap::kEmptyTile);
}

core::Status MapEditor::fillRect(tilemap::TileCoord from, tilemap::TileCoord to, tilemap::TileId tile) {
    const tilemap::TileBounds bounds = clampedBounds(from, to);
    if (bounds.isEmpty()) {
        return core::errorStatus(core::error_code::kOutOfRange, "rectangle is outside the map");
    }
    if (static_cast<std::size_t>(bounds.width) * static_cast<std::size_t>(bounds.height) > kMaxEditedCells) {
        return core::errorStatus(core::error_code::kOutOfRange, "rectangle exceeds the edit limit");
    }

    std::vector<TileChange> changes;
    changes.reserve(static_cast<std::size_t>(bounds.width) * static_cast<std::size_t>(bounds.height));
    for (int y = bounds.y; y < bounds.y + bounds.height; ++y) {
        for (int x = bounds.x; x < bounds.x + bounds.width; ++x) {
            const tilemap::TileId before = m_map.tileAt(x, y, m_layer);
            if (before == tile) {
                continue;
            }
            TileChange change;
            change.cell = tilemap::TileCoord{x, y};
            change.layer = m_layer;
            change.before = before;
            change.after = tile;
            changes.push_back(std::move(change));
        }
    }
    return applyChanges(std::move(changes), "rectangle");
}

core::Status MapEditor::floodFill(tilemap::TileCoord start, tilemap::TileId tile) {
    if (!m_map.contains(start.x, start.y)) {
        return core::errorStatus(core::error_code::kOutOfRange, "fill seed is outside the map");
    }
    const tilemap::TileId target = m_map.tileAt(start.x, start.y, m_layer);
    if (target == tile) {
        return core::okStatus();
    }

    const int width = m_map.width();
    const int height = m_map.height();
    std::vector<std::uint8_t> visited(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    std::vector<tilemap::TileCoord> stack;
    std::vector<TileChange> changes;
    stack.push_back(start);

    constexpr int kDx[4] = {1, -1, 0, 0};
    constexpr int kDy[4] = {0, 0, 1, -1};

    while (!stack.empty() && changes.size() < kMaxEditedCells) {
        const tilemap::TileCoord cell = stack.back();
        stack.pop_back();
        if (!m_map.contains(cell.x, cell.y)) {
            continue;
        }
        const std::size_t index =
            static_cast<std::size_t>(cell.y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(cell.x);
        if (visited[index] != 0) {
            continue;
        }
        visited[index] = 1;
        if (m_map.tileAt(cell.x, cell.y, m_layer) != target) {
            continue;
        }

        TileChange change;
        change.cell = cell;
        change.layer = m_layer;
        change.before = target;
        change.after = tile;
        changes.push_back(std::move(change));

        for (int neighbour = 0; neighbour < 4; ++neighbour) {
            stack.push_back(tilemap::TileCoord{cell.x + kDx[neighbour], cell.y + kDy[neighbour]});
        }
    }

    if (changes.empty()) {
        return core::okStatus();
    }
    return applyChanges(std::move(changes), "fill");
}

void MapEditor::cancelRect() noexcept {
    m_rectActive = false;
}

std::vector<tilemap::TileCoord> MapEditor::previewCells() const {
    std::vector<tilemap::TileCoord> cells;
    if (!m_rectActive) {
        return cells;
    }
    const tilemap::TileBounds bounds = clampedBounds(m_rectStart, m_hover);
    if (bounds.isEmpty()) {
        return cells;
    }
    cells.reserve(static_cast<std::size_t>(bounds.width) * static_cast<std::size_t>(bounds.height));
    for (int y = bounds.y; y < bounds.y + bounds.height; ++y) {
        for (int x = bounds.x; x < bounds.x + bounds.width; ++x) {
            cells.push_back(tilemap::TileCoord{x, y});
        }
    }
    return cells;
}

std::string MapEditor::mapText() const {
    return tilemap::saveTilemapToJson(m_map);
}

EditorStats MapEditor::stats() const {
    EditorStats stats;
    stats.tool = m_tool;
    stats.layer = m_layer;
    stats.brush = m_brush;
    stats.undoDepth = m_history.undoDepth();
    stats.redoDepth = m_history.redoDepth();
    stats.previewCells = previewCells().size();
    stats.commands = m_history.commands();
    stats.undos = m_history.undos();
    stats.redos = m_history.redos();
    stats.dirty = m_history.dirty();
    stats.mapWidth = m_map.width();
    stats.mapHeight = m_map.height();
    return stats;
}

core::Status MapEditor::applyChanges(std::vector<TileChange> changes, std::string label) {
    if (changes.empty()) {
        return core::okStatus();
    }
    for (const TileChange& change : changes) {
        if (!m_map.setTile(change.cell.x, change.cell.y, change.layer, change.after)) {
            return core::errorStatus(core::error_code::kOutOfRange, "editor change is outside the map");
        }
    }
    EditorCommand command;
    command.label = std::move(label);
    command.changes = std::move(changes);
    m_history.record(std::move(command));
    return core::okStatus();
}

tilemap::TileBounds MapEditor::clampedBounds(tilemap::TileCoord from, tilemap::TileCoord to) const {
    if (m_map.width() <= 0 || m_map.height() <= 0) {
        return tilemap::TileBounds{};
    }
    const int left = std::min(from.x, to.x);
    const int top = std::min(from.y, to.y);
    const int right = std::max(from.x, to.x);
    const int bottom = std::max(from.y, to.y);
    const int x0 = std::clamp(left, 0, m_map.width() - 1);
    const int y0 = std::clamp(top, 0, m_map.height() - 1);
    const int x1 = std::clamp(right, 0, m_map.width() - 1);
    const int y1 = std::clamp(bottom, 0, m_map.height() - 1);
    if (x1 < x0 || y1 < y0) {
        return tilemap::TileBounds{};
    }
    return tilemap::TileBounds{x0, y0, x1 - x0 + 1, y1 - y0 + 1};
}

}  // namespace fa::editor
