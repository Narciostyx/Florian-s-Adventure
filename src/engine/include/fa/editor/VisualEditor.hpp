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
struct TileDef;
struct MapBlocker;
}  // namespace fa::tilemap

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

    /// \brief Borrows the live tilemap; the editor paints into it immediately.
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
    /// \brief Callback that writes the placement table back to `data/entities.json`.
    void setPlacementSave(PlacementSaveHandler handler);
    /// \brief Map ids a portal may lead to. The app owns the map registry.
    void setPortalTargets(std::vector<std::string> targets);

    /// \brief Read-only entity list (spawn table) for the Entities scope; the app
    /// refreshes this when the editor opens.
    void setEntityInfos(std::vector<EntityInfoRow> rows);
    [[nodiscard]] std::span<const EntityInfoRow> entityInfos() const noexcept { return m_entityInfos; }

    /// \brief Current editing scope.
    [[nodiscard]] EditorScope scope() const noexcept { return m_scope; }
    /// \brief Switches scope; clears selections and any half-built portal.
    void setScope(EditorScope scope) noexcept;
    /// \brief The borrowed placement table (empty when tile-only mode).
    [[nodiscard]] std::span<const MapPlacement> placements() const noexcept;
    /// \brief The live map's collision zones (blocker scope data).
    [[nodiscard]] std::span<const tilemap::MapBlocker> blockers() const noexcept;

    /// \brief Show or hide the editor; hiding keeps undo history and selections.
    void setActive(bool active) noexcept;
    [[nodiscard]] bool active() const noexcept { return m_active; }
    /// \brief Toggle between shown and hidden.
    void toggle() noexcept { setActive(!m_active); }

    /// \brief Routes pointer input; `mapViewport` is in screen-space virtual pixels.
    void update(const VisualEditorInput& input, core::Rect mapViewport);

    /// \brief Draws the toolbar, palette and property inspector.
    void draw(ui::UiContext& ui, core::Rect screen, std::span<const std::string> textureIds, const SaveHandler& save);

    /// \brief Live accessors: the borrowed map, inner editor, history, last layout,
    /// hover cell, dirty flag, serialized map text and the current status line. @{
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
    /// @}

    /// \brief Whether the footer's 返回 was clicked; the app closes the window.
    [[nodiscard]] bool closeRequested() const noexcept { return m_closeRequested; }
    void clearCloseRequest() noexcept { m_closeRequested = false; }

    /// \brief Right-hand panel rectangle for a screen rect.
    [[nodiscard]] static core::Rect panelRect(core::Rect screen) noexcept;
    /// \brief Left-hand map viewport rectangle for a screen rect.
    [[nodiscard]] static core::Rect mapRect(core::Rect screen) noexcept;
    /// \brief The lower-right inspector rectangle, shared by the panels and the footer.
    [[nodiscard]] static core::Rect propsRect(core::Rect screen) noexcept;

private:
    /// \brief Rebuilds the map at the new size, copying overlapping cells and defs.
    void resizeMap(int width, int height);
    /// \brief Scope/tool buttons row across the top.
    void drawToolbar(ui::UiContext& ui, core::Rect screen);
    /// \brief Tile or object palette (upper right), per scope.
    void drawPalette(ui::UiContext& ui, core::Rect screen, std::span<const std::string> textureIds);
    /// \brief Palette for the placement/portal scopes: kinds on the left, list on the right.
    void drawObjectPalette(ui::UiContext& ui, core::Rect palette);
    /// \brief Component palette (decor scope): the grass/flower/tree defs.
    void drawDecorPalette(ui::UiContext& ui, core::Rect palette);
    /// \brief Inspector for the decor scope: the selected component and its rules.
    void drawDecorProperties(ui::UiContext& ui, core::Rect props);
    /// \brief Blocker list (collision-zone scope).
    void drawBlockerPalette(ui::UiContext& ui, core::Rect palette);
    /// \brief Inspector for the selected collision zone: texture, height, door, delete.
    void drawBlockerProperties(ui::UiContext& ui, core::Rect props, std::span<const std::string> textureIds);
    /// \brief Inspector for the placement/portal scopes.
    void drawObjectProperties(ui::UiContext& ui, core::Rect props);
    /// \brief Entity list panel (read-only scope): one button per spawn row.
    void drawEntityPalette(ui::UiContext& ui, core::Rect palette);
    /// \brief Inspector for the selected entity row: JSON field name and value.
    void drawEntityProperties(ui::UiContext& ui, core::Rect props);
    /// \brief 保存 button plus the status line, drawn in every scope: switching to the object
    /// or portal scope must not strand the session without a way to write its work out.
    void drawFooter(ui::UiContext& ui, core::Rect screen, const SaveHandler& save);
    /// \brief Tile-scope inspector: map settings and the selected tile definition.
    void drawProperties(ui::UiContext& ui,
                        core::Rect screen,
                        std::span<const std::string> textureIds,
                        const SaveHandler& save);
    /// \brief Maps a screen cursor onto a grid cell (and the tile scale).
    void updateHover(const core::Vec2& cursor);
    /// \brief Places or selects at \p cell (placement scope).
    void applyPlacementClick(tilemap::TileCoord cell);
    /// \brief Removes the placement at \p cell.
    void applyPlacementErase(tilemap::TileCoord cell);
    /// \brief First click sets the corner, second builds the region (portal scope).
    void applyPortalClick(tilemap::TileCoord cell);
    /// \brief Places the selected component at \p cell (decor scope); the ground
    /// tile under the cell must be grass and is never replaced.
    void applyDecorClick(tilemap::TileCoord cell);
    /// \brief Clears the decor cell at \p cell (decor scope, right click).
    void applyDecorErase(tilemap::TileCoord cell);
    /// \brief First click sets the corner, second builds the collision zone (blocker scope).
    void applyBlockerClick(tilemap::TileCoord cell);
    /// \brief Index of the layer components stand on: "decor", else the
    /// ground-detail layer, else created on demand. -1 when unusable.
    [[nodiscard]] int decorLayerIndex();
    /// \brief Whether the ground tile at \p cell is grass (decor placement rule).
    [[nodiscard]] bool isGrassGround(tilemap::TileCoord cell) const;
    /// \brief Whether a definition belongs to the component palette, not the tile one.
    [[nodiscard]] static bool isComponentDef(const tilemap::TileDef& def) noexcept;
    /// \brief Row indices at \p cell; -1 when nothing is there. @{
    [[nodiscard]] int placementIndexAt(tilemap::TileCoord cell) const noexcept;
    [[nodiscard]] int portalIndexAt(tilemap::TileCoord cell) const noexcept;
    [[nodiscard]] int blockerIndexAt(tilemap::TileCoord cell) const noexcept;
    [[nodiscard]] int entityIndexAt(tilemap::TileCoord cell) const noexcept;
    /// @}
    /// \brief Runs the placement save callback when the app supplied one.
    void saveObjects();

    EditorHistory m_history;
    MapEditor m_editor;
    bool m_active = false;
    bool m_closeRequested = false;
    core::Rect m_mapViewport{};
    float m_tileScale = 1.0f;
    tilemap::TileId m_selectedTile{};
    /// \brief Selected component definition in the decor scope (grass/flower/tree).
    tilemap::TileId m_decorBrush{};
    int m_selectedTexture = 0;
    std::string m_status;
    EditorScope m_scope = EditorScope::Tiles;
    /// \brief Borrowed from the composition root; null when the app has no spawn table.
    std::vector<MapPlacement>* m_placements = nullptr;
    std::vector<PlacementKind> m_kinds;
    PlacementSaveHandler m_placementSave;
    std::vector<std::string> m_portalTargets;
    /// \brief Read-only spawn rows for the Entities scope; owned by the editor between opens.
    std::vector<EntityInfoRow> m_entityInfos;
    /// \brief Selected row in `m_placements` / `map().portals()`; -1 when nothing is picked.
    int m_selectedPlacement = -1;
    int m_selectedPortal = -1;
    int m_selectedBlocker = -1;
    int m_selectedEntity = -1;
    int m_activeKind = 0;
    /// \brief 单击查看 / 双击替换（笔刷工具）：距首次点击的帧数，-1=无待确认点击。
    static constexpr int kDoubleClickFrames = 15;
    int m_framesSinceClick = -1;
    tilemap::TileCoord m_lastClickCell{-1, -1};
    /// \brief Tile definition the inspector shows after a single click; falls back to
    /// the brush when nothing has been inspected yet.
    tilemap::TileId m_inspectedTile{};
    tilemap::TileCoord m_inspectedCell{-1, -1};
    bool m_hasInspected = false;
    /// \brief First corner of a portal that is mid-creation (click-click, like the rect tool).
    std::optional<tilemap::TileCoord> m_portalStart;
    /// \brief First corner of a collision zone that is mid-creation (click-click).
    std::optional<tilemap::TileCoord> m_blockerStart;
};

}  // namespace fa::editor