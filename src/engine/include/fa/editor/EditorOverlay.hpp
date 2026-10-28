#pragma once

#include <string>
#include <vector>

#include "fa/core/Math.hpp"
#include "fa/editor/EditorHistory.hpp"
#include "fa/editor/EditorTypes.hpp"
#include "fa/editor/MapEditor.hpp"

namespace fa::ui {
class UiContext;
}

namespace fa::tilemap {
class Tilemap;
}

namespace fa::editor {

/// \file
/// In-game editor overlay: active flag, mouse routing and a compact status panel.
///
/// The overlay owns no map memory; it borrows the scene's `Tilemap`, so an edit
/// appears in the next extraction pass. Keyboard shortcuts are routed by the
/// composition root (F2 toggle, 1–5 tools, brackets layer, Z/Y undo/redo).

/// \brief Editor controller and status panel.
class EditorOverlay {
public:
    explicit EditorOverlay(tilemap::Tilemap& map);

    EditorOverlay(const EditorOverlay&) = delete;
    EditorOverlay& operator=(const EditorOverlay&) = delete;
    EditorOverlay(EditorOverlay&&) = delete;
    EditorOverlay& operator=(EditorOverlay&&) = delete;

    void toggle() noexcept { m_active = !m_active; }
    void setActive(bool active) noexcept { m_active = active; }
    [[nodiscard]] bool active() const noexcept { return m_active; }

    /// \brief Routes one frame of pointer input while active.
    void update(const EditorFrameInput& input);

    /// \brief Draws the status panel through M09. Call before `UiContext::submit()`.
    void draw(ui::UiContext& ui) const;

    void setTool(EditorTool tool) noexcept { m_editor.setTool(tool); }
    void setLayer(int layer) noexcept { m_editor.setLayer(layer); }
    void setBrush(tilemap::TileId tile) noexcept { m_editor.setBrush(tile); }

    void undo();
    void redo();

    [[nodiscard]] tilemap::TileCoord hover() const noexcept { return m_editor.hover(); }
    [[nodiscard]] std::vector<tilemap::TileCoord> previewCells() const { return m_editor.previewCells(); }

    [[nodiscard]] MapEditor& editor() noexcept { return m_editor; }
    [[nodiscard]] const MapEditor& editor() const noexcept { return m_editor; }
    [[nodiscard]] EditorStats stats() const;
    [[nodiscard]] std::string mapText() const { return m_editor.mapText(); }

    /// \brief Whether a viewport-space cursor is over the status panel.
    [[nodiscard]] bool overPanel(core::Vec2 screen) const noexcept;

private:
    [[nodiscard]] core::Rect panelRect() const noexcept;

    EditorHistory m_history;
    MapEditor m_editor;
    bool m_active = false;
    EditorFrameInput m_input;
};

}  // namespace fa::editor
