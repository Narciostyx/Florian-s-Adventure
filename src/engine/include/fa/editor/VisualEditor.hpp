#pragma once

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>

#include "fa/core/Math.hpp"
#include "fa/core/Result.hpp"
#include "fa/editor/EditorHistory.hpp"
#include "fa/editor/EditorTypes.hpp"
#include "fa/editor/MapEditor.hpp"
#include "fa/tilemap/TileId.hpp"

namespace fa::tilemap {
class Tilemap;
}

namespace fa::ui {
class UiContext;
}

namespace fa::editor {

/// \file
/// Visual editor shell for M26: a left map viewport, a top-right component
/// palette and a bottom-right property inspector.
///
/// The editor owns only UI state and an undo history; the map is borrowed from the
/// scene so a painted cell is visible on the next extraction pass. Saving is a
/// callback because file access belongs to the composition root.

/// \brief Result of one frame of editor input.
struct VisualEditorInput {
    core::Vec2 cursorScreen{};
    bool primaryPressed = false;
    bool secondaryPressed = false;
};

/// \brief Left-map / right-panel editor controller.
class VisualEditor {
public:
    /// \brief Callback that writes the serialized map text and reports I/O errors.
    using SaveHandler = std::function<core::Status(std::string_view)>;

    explicit VisualEditor(tilemap::Tilemap& map);

    VisualEditor(const VisualEditor&) = delete;
    VisualEditor& operator=(const VisualEditor&) = delete;
    VisualEditor(VisualEditor&&) = delete;
    VisualEditor& operator=(VisualEditor&&) = delete;

    void setActive(bool active) noexcept;
    [[nodiscard]] bool active() const noexcept { return m_active; }
    void toggle() noexcept { setActive(!m_active); }

    /// \brief Routes pointer input; `mapViewport` is in screen-space virtual pixels.
    void update(const VisualEditorInput& input, core::Rect mapViewport);

    /// \brief Draws the toolbar, palette and property inspector.
    void draw(ui::UiContext& ui, core::Rect screen, std::span<const std::string> textureIds, const SaveHandler& save);

    [[nodiscard]] tilemap::Tilemap& map() noexcept { return m_editor.map(); }
    [[nodiscard]] const tilemap::Tilemap& map() const noexcept { return m_editor.map(); }
    [[nodiscard]] MapEditor& editor() noexcept { return m_editor; }
    [[nodiscard]] EditorHistory& history() noexcept { return m_history; }
    [[nodiscard]] core::Rect mapViewport() const noexcept { return m_mapViewport; }
    [[nodiscard]] float tileScale() const noexcept { return m_tileScale; }
    [[nodiscard]] tilemap::TileCoord hover() const noexcept { return m_editor.hover(); }
    [[nodiscard]] bool dirty() const noexcept { return m_history.dirty(); }
    [[nodiscard]] std::string mapText() const { return m_editor.mapText(); }
    [[nodiscard]] std::string_view status() const noexcept { return m_status; }
    [[nodiscard]] bool closeRequested() const noexcept { return m_closeRequested; }
    void clearCloseRequest() noexcept { m_closeRequested = false; }

    [[nodiscard]] static core::Rect panelRect(core::Rect screen) noexcept;
    [[nodiscard]] static core::Rect mapRect(core::Rect screen) noexcept;

private:
    void resizeMap(int width, int height);
    void drawToolbar(ui::UiContext& ui, core::Rect screen);
    void drawPalette(ui::UiContext& ui, core::Rect screen, std::span<const std::string> textureIds);
    void drawProperties(ui::UiContext& ui,
                        core::Rect screen,
                        std::span<const std::string> textureIds,
                        const SaveHandler& save);
    void updateHover(const core::Vec2& cursor);

    EditorHistory m_history;
    MapEditor m_editor;
    bool m_active = false;
    bool m_closeRequested = false;
    core::Rect m_mapViewport{};
    float m_tileScale = 1.0f;
    tilemap::TileId m_selectedTile{};
    int m_selectedTexture = 0;
    std::string m_status;
};

}  // namespace fa::editor