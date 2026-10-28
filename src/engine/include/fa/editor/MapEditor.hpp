#pragma once

#include <string>
#include <vector>

#include "fa/core/Result.hpp"
#include "fa/editor/EditorHistory.hpp"
#include "fa/editor/EditorTypes.hpp"

namespace fa::tilemap {
class Tilemap;
}

namespace fa::editor {

/// \file
/// Tile editing operations for M26. The editor deliberately works on `Tilemap`
/// directly: it is the same data the runtime extracts, so a painted cell is visible
/// on the next extraction frame without an import step.

/// \brief Brush/rect/fill/erase/picker operations with undo support.
class MapEditor {
public:
    MapEditor(tilemap::Tilemap& map, EditorHistory& history);

    MapEditor(const MapEditor&) = delete;
    MapEditor& operator=(const MapEditor&) = delete;
    MapEditor(MapEditor&&) = delete;
    MapEditor& operator=(MapEditor&&) = delete;

    void setLayer(int layer) noexcept;
    [[nodiscard]] int layer() const noexcept { return m_layer; }

    void setBrush(tilemap::TileId tile) noexcept { m_brush = tile; }
    [[nodiscard]] tilemap::TileId brush() const noexcept { return m_brush; }

    void setTool(EditorTool tool) noexcept { m_tool = tool; }
    [[nodiscard]] EditorTool tool() const noexcept { return m_tool; }

    void setHover(tilemap::TileCoord cell) noexcept { m_hover = cell; }
    [[nodiscard]] tilemap::TileCoord hover() const noexcept { return m_hover; }

    /// \brief Runs the active tool at `hover()`.
    [[nodiscard]] core::Status applyPrimary();
    /// \brief Cancels a pending rectangle, or erases the hover cell.
    [[nodiscard]] core::Status applySecondary();

    [[nodiscard]] core::Status paint(tilemap::TileCoord cell, tilemap::TileId tile);
    [[nodiscard]] core::Status erase(tilemap::TileCoord cell);
    [[nodiscard]] core::Status fillRect(tilemap::TileCoord from, tilemap::TileCoord to, tilemap::TileId tile);
    [[nodiscard]] core::Status floodFill(tilemap::TileCoord start, tilemap::TileId tile);

    void cancelRect() noexcept;
    [[nodiscard]] bool rectActive() const noexcept { return m_rectActive; }
    [[nodiscard]] std::vector<tilemap::TileCoord> previewCells() const;

    [[nodiscard]] tilemap::Tilemap& map() noexcept { return m_map; }
    [[nodiscard]] const tilemap::Tilemap& map() const noexcept { return m_map; }
    [[nodiscard]] EditorHistory& history() noexcept { return m_history; }
    [[nodiscard]] const EditorHistory& history() const noexcept { return m_history; }

    /// \brief The map document in the runtime JSON format.
    [[nodiscard]] std::string mapText() const;
    [[nodiscard]] EditorStats stats() const;

private:
    [[nodiscard]] core::Status applyChanges(std::vector<TileChange> changes, std::string label);
    [[nodiscard]] tilemap::TileBounds clampedBounds(tilemap::TileCoord from, tilemap::TileCoord to) const;

    tilemap::Tilemap& m_map;
    EditorHistory& m_history;
    int m_layer = 0;
    tilemap::TileId m_brush{};
    EditorTool m_tool = EditorTool::Brush;
    tilemap::TileCoord m_hover{};
    tilemap::TileCoord m_rectStart{};
    bool m_rectActive = false;
};

}  // namespace fa::editor
