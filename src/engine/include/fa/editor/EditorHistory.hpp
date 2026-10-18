#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "fa/editor/EditorTypes.hpp"

namespace fa::tilemap {
class Tilemap;
}

namespace fa::editor {

/// \file
/// Command history for the editor. Commands own only the changed cells, so undo and
/// redo apply a handful of `setTile` calls instead of copying the map.

/// \brief Bounded undo/redo stack.
class EditorHistory {
public:
    explicit EditorHistory(std::size_t capacity = 128);

    void setCapacity(std::size_t capacity);

    /// \brief Records a completed command and clears the redo stack.
    void record(EditorCommand command);

    [[nodiscard]] bool undo(tilemap::Tilemap& map);
    [[nodiscard]] bool redo(tilemap::Tilemap& map);

    [[nodiscard]] bool canUndo() const noexcept { return !m_undo.empty(); }
    [[nodiscard]] bool canRedo() const noexcept { return !m_redo.empty(); }
    [[nodiscard]] std::size_t undoDepth() const noexcept { return m_undo.size(); }
    [[nodiscard]] std::size_t redoDepth() const noexcept { return m_redo.size(); }
    [[nodiscard]] std::size_t capacity() const noexcept { return m_capacity; }

    [[nodiscard]] bool dirty() const noexcept { return m_dirty; }
    void markSaved() noexcept { m_dirty = false; }
    void clear() noexcept;

    [[nodiscard]] std::uint64_t commands() const noexcept { return m_commands; }
    [[nodiscard]] std::uint64_t undos() const noexcept { return m_undos; }
    [[nodiscard]] std::uint64_t redos() const noexcept { return m_redos; }

private:
    static void apply(tilemap::Tilemap& map, const EditorCommand& command, bool after);

    std::size_t m_capacity = 128;
    std::vector<EditorCommand> m_undo;
    std::vector<EditorCommand> m_redo;
    bool m_dirty = false;
    std::uint64_t m_commands = 0;
    std::uint64_t m_undos = 0;
    std::uint64_t m_redos = 0;
};

}  // namespace fa::editor
