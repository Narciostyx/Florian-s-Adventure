#include "fa/editor/EditorHistory.hpp"

#include <utility>

#include "fa/tilemap/Tilemap.hpp"

namespace fa::editor {

std::string_view toString(EditorTool tool) noexcept {
    switch (tool) {
        case EditorTool::Brush:
            return "brush";
        case EditorTool::Rect:
            return "rect";
        case EditorTool::Fill:
            return "fill";
        case EditorTool::Erase:
            return "erase";
        case EditorTool::Picker:
            return "picker";
        default:
            return "?";
    }
}

EditorHistory::EditorHistory(std::size_t capacity) : m_capacity(capacity == 0 ? 1 : capacity) {}

void EditorHistory::setCapacity(std::size_t capacity) {
    m_capacity = capacity == 0 ? 1 : capacity;
    while (m_undo.size() > m_capacity) {
        m_undo.erase(m_undo.begin());
    }
}

void EditorHistory::record(EditorCommand command) {
    if (command.changes.empty()) {
        return;
    }
    m_redo.clear();
    m_undo.push_back(std::move(command));
    while (m_undo.size() > m_capacity) {
        m_undo.erase(m_undo.begin());
    }
    m_dirty = true;
    ++m_commands;
}

bool EditorHistory::undo(tilemap::Tilemap& map) {
    if (m_undo.empty()) {
        return false;
    }
    EditorCommand command = std::move(m_undo.back());
    m_undo.pop_back();
    apply(map, command, false);
    m_redo.push_back(std::move(command));
    m_dirty = true;
    ++m_undos;
    return true;
}

bool EditorHistory::redo(tilemap::Tilemap& map) {
    if (m_redo.empty()) {
        return false;
    }
    EditorCommand command = std::move(m_redo.back());
    m_redo.pop_back();
    apply(map, command, true);
    m_undo.push_back(std::move(command));
    m_dirty = true;
    ++m_redos;
    return true;
}

void EditorHistory::clear() noexcept {
    m_undo.clear();
    m_redo.clear();
    m_dirty = false;
    m_commands = 0;
    m_undos = 0;
    m_redos = 0;
}

void EditorHistory::apply(tilemap::Tilemap& map, const EditorCommand& command, bool after) {
    for (const TileChange& change : command.changes) {
        static_cast<void>(
            map.setTile(change.cell.x, change.cell.y, change.layer, after ? change.after : change.before));
    }
}

}  // namespace fa::editor
