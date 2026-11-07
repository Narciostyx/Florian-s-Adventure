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
    /// \brief Keeps the newest \p capacity commands.
    explicit EditorHistory(std::size_t capacity = 128);

    /// \brief Resizes the stacks, dropping the oldest commands when smaller.
    void setCapacity(std::size_t capacity);

    /// \brief Records a completed command and clears the redo stack.
    void record(EditorCommand command);

    /// \brief Rewinds the newest command into \p map. \return false when empty.
    [[nodiscard]] bool undo(tilemap::Tilemap& map);
    /// \brief Re-applies the newest undone command. \return false when empty.
    [[nodiscard]] bool redo(tilemap::Tilemap& map);

    /// \brief Stack state queries. @{
    [[nodiscard]] bool canUndo() const noexcept { return !m_undo.empty(); }
    [[nodiscard]] bool canRedo() const noexcept { return !m_redo.empty(); }
    [[nodiscard]] std::size_t undoDepth() const noexcept { return m_undo.size(); }
    [[nodiscard]] std::size_t redoDepth() const noexcept { return m_redo.size(); }
    [[nodiscard]] std::size_t capacity() const noexcept { return m_capacity; }
    /// @}

    /// \brief Whether the map changed since the last save.
    [[nodiscard]] bool dirty() const noexcept { return m_dirty; }
    /// \brief Flags unsaved work that is not a tile change (placements, portals).
    void markDirty() noexcept { m_dirty = true; }
    /// \brief Clears the dirty flag after a successful save.
    void markSaved() noexcept { m_dirty = false; }
    /// \brief Empties both stacks and the dirty flag; counters are cumulative.
    void clear() noexcept;

    /// \brief Cumulative command/undo/redo counters for the smoke summary. @{
    [[nodiscard]] std::uint64_t commands() const noexcept { return m_commands; }
    [[nodiscard]] std::uint64_t undos() const noexcept { return m_undos; }
    [[nodiscard]] std::uint64_t redos() const noexcept { return m_redos; }
    /// @}

private:
    /// \brief Applies (or reverts) one command's cell changes.
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
