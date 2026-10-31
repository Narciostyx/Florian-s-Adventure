#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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
    /// \brief Callback that writes the placement table; the app owns `data/entities.json`.
    using PlacementSaveHandler = std::function<core::Status(const std::vector<MapPlacement>&)>;

    explicit VisualEditor(tilemap::Tilemap& map);

    VisualEditor(const VisualEditor&) = delete;
    VisualEditor& operator=(const VisualEditor&) = delete;
    VisualEditor(VisualEditor&&) = delete;
    VisualEditor& operator=(VisualEditor&&) = delete;

    /// \brief Borrows the placement table the app loaded and the kinds the palette lists.
    ///
    /// The editor writes into the vector in place; nothing is persisted until the save
    /// callback runs, so a discarded session leaves `entities.json` untouched. Passing a
    /// null pointer returns the editor to tile-only mode.
    void setPlacements(std::vector<MapPlacement>* placements, std::vector<PlacementKind> kinds);
    void setPlacementSave(PlacementSaveHandler handler);
    /// \brief Map ids a portal may lead to. The app owns the map registry.
    void setPortalTargets(std::vector<std::string> targets);

    [[nodiscard]] EditorScope scope() const noexcept { return m_scope; }
    void setScope(EditorScope scope) noexcept;
    [[nodiscard]] std::span<const MapPlacement> placements() const noexcept;

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
    /// \brief The lower-right inspector rectangle, shared by the panels and the footer.
    [[nodiscard]] static core::Rect propsRect(core::Rect screen) noexcept;

private:
    void resizeMap(int width, int height);
    void drawToolbar(ui::UiContext& ui, core::Rect screen);
    void drawPalette(ui::UiContext& ui, core::Rect screen, std::span<const std::string> textureIds);
    /// \brief Palette for the placement/portal scopes: kinds on the left, list on the right.
    void drawObjectPalette(ui::UiContext& ui, core::Rect palette);
    /// \brief Inspector for the placement/portal scopes.
    void drawObjectProperties(ui::UiContext& ui, core::Rect props);
    /// \brief 保存 button plus the status line, drawn in every scope: switching to the object
    /// or portal scope must not strand the session without a way to write its work out.
    void drawFooter(ui::UiContext& ui, core::Rect screen, const SaveHandler& save);
    void drawProperties(ui::UiContext& ui,
                        core::Rect screen,
                        std::span<const std::string> textureIds,
                        const SaveHandler& save);
    void updateHover(const core::Vec2& cursor);
    void applyPlacementClick(tilemap::TileCoord cell);
    void applyPlacementErase(tilemap::TileCoord cell);
    void applyPortalClick(tilemap::TileCoord cell);
    [[nodiscard]] int placementIndexAt(tilemap::TileCoord cell) const noexcept;
    [[nodiscard]] int portalIndexAt(tilemap::TileCoord cell) const noexcept;
    void saveObjects();

    EditorHistory m_history;
    MapEditor m_editor;
    bool m_active = false;
    bool m_closeRequested = false;
    core::Rect m_mapViewport{};
    float m_tileScale = 1.0f;
    tilemap::TileId m_selectedTile{};
    int m_selectedTexture = 0;
    std::string m_status;
    EditorScope m_scope = EditorScope::Tiles;
    /// \brief Borrowed from the composition root; null when the app has no spawn table.
    std::vector<MapPlacement>* m_placements = nullptr;
    std::vector<PlacementKind> m_kinds;
    PlacementSaveHandler m_placementSave;
    std::vector<std::string> m_portalTargets;
    /// \brief Selected row in `m_placements` / `map().portals()`; -1 when nothing is picked.
    int m_selectedPlacement = -1;
    int m_selectedPortal = -1;
    int m_activeKind = 0;
    /// \brief First corner of a portal that is mid-creation (click-click, like the rect tool).
    std::optional<tilemap::TileCoord> m_portalStart;
};

}  // namespace fa::editor