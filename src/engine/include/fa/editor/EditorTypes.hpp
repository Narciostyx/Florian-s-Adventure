#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "fa/core/Math.hpp"
#include "fa/tilemap/TileId.hpp"

namespace fa::editor {

/// \file
/// Value types for M26: tools, tile diffs, undo commands, overlay input and stats.
///
/// An edit is stored as a small set of `before`/`after` tile ids, not as a whole-map
/// snapshot. That keeps undo cheap on the 40x23 demo map and scales to larger maps.

/// \brief Active editing tool.
enum class EditorTool : std::uint8_t {
    Brush = 0,
    Rect,
    Fill,
    Erase,
    Picker,
    Count,
};

/// \brief Stable name for the panel, logs and tests.
[[nodiscard]] std::string_view toString(EditorTool tool) noexcept;

/// \brief One cell changed by a command.
struct TileChange {
    tilemap::TileCoord cell{};
    int layer = 0;
    tilemap::TileId before{};
    tilemap::TileId after{};
};

/// \brief One undoable operation.
struct EditorCommand {
    std::string label;
    std::vector<TileChange> changes;
};

/// \brief One frame of overlay input, in world and viewport coordinates.
struct EditorFrameInput {
    core::Vec2 cursorWorld{};
    core::Vec2 cursorScreen{};
    bool primaryDown = false;
    bool primaryPressed = false;
    bool primaryReleased = false;
    bool secondaryPressed = false;
};

/// \brief Snapshot for the smoke summary and M27.
struct EditorStats {
    bool active = false;
    EditorTool tool = EditorTool::Brush;
    int layer = 0;
    tilemap::TileId brush{};
    std::size_t undoDepth = 0;
    std::size_t redoDepth = 0;
    std::size_t previewCells = 0;
    std::uint64_t commands = 0;
    std::uint64_t undos = 0;
    std::uint64_t redos = 0;
    bool dirty = false;
    int mapWidth = 0;
    int mapHeight = 0;
};

}  // namespace fa::editor
