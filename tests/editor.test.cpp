/// \file
/// \brief Tests for M26: undo/redo, brush/rect/fill/erase/picker, JSON round-trip,
/// overlay activation/input and the 50-step undo acceptance case.

#include <catch2/catch_test_macros.hpp>

#include "fa/editor/EditorHistory.hpp"
#include "fa/editor/EditorOverlay.hpp"
#include "fa/editor/MapEditor.hpp"
#include "fa/editor/VisualEditor.hpp"
#include "fa/tilemap/Tilemap.hpp"
#include "fa/tilemap/TilemapLoader.hpp"
#include "fa/tilemap/TilemapWriter.hpp"
#include "fa/ui/Ui.hpp"

namespace {

using fa::editor::EditorFrameInput;
using fa::editor::EditorHistory;
using fa::editor::EditorOverlay;
using fa::editor::EditorTool;
using fa::editor::MapEditor;
using fa::tilemap::AreaKind;
using fa::tilemap::MapAmbient;
using fa::tilemap::MapArea;
using fa::tilemap::MapPortal;
using fa::tilemap::TileCoord;
using fa::tilemap::TileDef;
using fa::tilemap::TileId;
using fa::tilemap::TileLayer;
using fa::tilemap::Tilemap;

constexpr std::uint16_t kGrassTile = 1;
constexpr std::uint16_t kSoilTile = 2;
constexpr std::uint16_t kWallTile = 3;

Tilemap makeMap() {
    Tilemap map;
    REQUIRE(map.reset(8, 8, 16).isOk());

    TileDef grass;
    grass.id = TileId{kGrassTile};
    grass.name = "grass";
    grass.walkable = true;
    REQUIRE(map.addTileDef(grass));

    TileDef soil;
    soil.id = TileId{kSoilTile};
    soil.name = "soil";
    soil.walkable = true;
    soil.tillable = true;
    REQUIRE(map.addTileDef(soil));

    TileDef wall;
    wall.id = TileId{kWallTile};
    wall.name = "wall";
    wall.walkable = false;
    wall.solid = true;
    REQUIRE(map.addTileDef(wall));

    TileLayer layer;
    layer.name = "ground";
    layer.tiles.assign(map.cellCount(), TileId{kGrassTile});
    REQUIRE(map.addLayer(layer) >= 0);
    return map;
}

TileId tileAt(const Tilemap& map, int x, int y) {
    return map.tileAt(x, y, 0);
}

}  // namespace

TEST_CASE("Editor history paints, undoes and redoes", "[editor]") {
    Tilemap map = makeMap();
    EditorHistory history;
    MapEditor editor(map, history);

    REQUIRE(editor.paint(TileCoord{1, 1}, TileId{kSoilTile}).isOk());
    CHECK(tileAt(map, 1, 1) == TileId{kSoilTile});
    CHECK(history.undoDepth() == 1);
    CHECK(history.dirty());
    CHECK(history.commands() == 1);

    REQUIRE(history.undo(map));
    CHECK(tileAt(map, 1, 1) == TileId{kGrassTile});
    CHECK(history.redoDepth() == 1);
    CHECK(history.undos() == 1);

    REQUIRE(history.redo(map));
    CHECK(tileAt(map, 1, 1) == TileId{kSoilTile});
    CHECK(history.redos() == 1);
}

TEST_CASE("Recording a new command clears redo", "[editor]") {
    Tilemap map = makeMap();
    EditorHistory history(3);
    MapEditor editor(map, history);

    for (int x = 0; x < 4; ++x) {
        REQUIRE(editor.paint(TileCoord{x, 0}, TileId{kSoilTile}).isOk());
    }
    CHECK(history.undoDepth() == 3);
    CHECK(history.capacity() == 3);

    REQUIRE(history.undo(map));
    REQUIRE(history.undo(map));
    CHECK(history.redoDepth() == 2);

    REQUIRE(editor.paint(TileCoord{5, 0}, TileId{kSoilTile}).isOk());
    CHECK(history.redoDepth() == 0);
}

TEST_CASE("Fifty edits undo and redo without corruption", "[editor]") {
    Tilemap map = makeMap();
    EditorHistory history(64);
    MapEditor editor(map, history);

    for (int index = 0; index < 50; ++index) {
        REQUIRE(editor.paint(TileCoord{index % 8, index / 8}, TileId{kSoilTile}).isOk());
    }
    CHECK(history.undoDepth() == 50);
    for (int index = 0; index < 50; ++index) {
        REQUIRE(history.undo(map));
    }
    CHECK_FALSE(history.canUndo());
    CHECK(history.redoDepth() == 50);
    for (int x = 0; x < 8; ++x) {
        CHECK(tileAt(map, x, 0) == TileId{kGrassTile});
    }

    for (int index = 0; index < 50; ++index) {
        REQUIRE(history.redo(map));
    }
    CHECK(history.redoDepth() == 0);
    CHECK(tileAt(map, 1, 0) == TileId{kSoilTile});
}

TEST_CASE("Rect tool previews and fills a rectangle", "[editor]") {
    Tilemap map = makeMap();
    EditorHistory history;
    MapEditor editor(map, history);
    editor.setTool(EditorTool::Rect);
    editor.setBrush(TileId{kSoilTile});

    editor.setHover(TileCoord{1, 1});
    REQUIRE(editor.applyPrimary().isOk());
    CHECK(editor.rectActive());
    CHECK(editor.previewCells().size() == 1);

    editor.setHover(TileCoord{3, 3});
    CHECK(editor.previewCells().size() == 9);
    REQUIRE(editor.applyPrimary().isOk());
    CHECK_FALSE(editor.rectActive());
    CHECK(history.undoDepth() == 1);

    for (int y = 1; y <= 3; ++y) {
        for (int x = 1; x <= 3; ++x) {
            CHECK(tileAt(map, x, y) == TileId{kSoilTile});
        }
    }
    CHECK(tileAt(map, 0, 0) == TileId{kGrassTile});
}

TEST_CASE("Fill stays inside a contiguous region", "[editor]") {
    Tilemap map = makeMap();
    for (int y = 0; y < 8; ++y) {
        REQUIRE(map.setTile(4, y, 0, TileId{kWallTile}));
    }
    EditorHistory history;
    MapEditor editor(map, history);
    editor.setTool(EditorTool::Fill);
    editor.setBrush(TileId{kSoilTile});

    editor.setHover(TileCoord{1, 1});
    REQUIRE(editor.applyPrimary().isOk());

    CHECK(tileAt(map, 3, 7) == TileId{kSoilTile});
    CHECK(tileAt(map, 4, 1) == TileId{kWallTile});
    CHECK(tileAt(map, 5, 1) == TileId{kGrassTile});
    CHECK(history.undoDepth() == 1);
}

TEST_CASE("Erase and picker update the brush and cell", "[editor]") {
    Tilemap map = makeMap();
    EditorHistory history;
    MapEditor editor(map, history);

    REQUIRE(editor.paint(TileCoord{2, 2}, TileId{kSoilTile}).isOk());
    editor.setTool(EditorTool::Picker);
    editor.setHover(TileCoord{2, 2});
    REQUIRE(editor.applyPrimary().isOk());
    CHECK(editor.brush() == TileId{kSoilTile});

    editor.setTool(EditorTool::Erase);
    editor.setHover(TileCoord{2, 2});
    REQUIRE(editor.applyPrimary().isOk());
    CHECK(tileAt(map, 2, 2) == fa::tilemap::kEmptyTile);
}

TEST_CASE("Map JSON round-trips through the writer and loader", "[editor]") {
    Tilemap map = makeMap();
    MapArea area;
    area.areaId = 7;
    area.kind = AreaKind::Greenhouse;
    area.bounds = fa::tilemap::TileBounds{1, 1, 2, 2};
    map.addArea(area);

    MapPortal portal;
    portal.id = "to-field";
    portal.bounds = fa::tilemap::TileBounds{6, 6, 1, 1};
    portal.targetMap = "field";
    portal.targetTile = TileCoord{2, 3};
    map.addPortal(portal);

    MapAmbient ambient;
    ambient.indoor = true;
    ambient.light = fa::core::Color{0.9f, 0.8f, 0.7f, 1.0f};
    ambient.music = "night";
    map.setAmbient(ambient);
    REQUIRE(map.setTile(3, 3, 0, TileId{kSoilTile}));

    const std::string text = fa::tilemap::saveTilemapToJson(map);
    const auto loaded = fa::tilemap::loadTilemapFromJson(text);
    REQUIRE(loaded.isOk());
    const Tilemap& copy = loaded.value();
    CHECK(copy.width() == map.width());
    CHECK(copy.height() == map.height());
    CHECK(copy.tileSize() == map.tileSize());
    CHECK(copy.tileDefCount() == map.tileDefCount());
    CHECK(copy.layerCount() == map.layerCount());
    CHECK(tileAt(copy, 3, 3) == TileId{kSoilTile});
    REQUIRE(copy.areas().size() == 1);
    CHECK(copy.areas().front().areaId == 7);
    CHECK(copy.areas().front().kind == AreaKind::Greenhouse);
    REQUIRE(copy.portals().size() == 1);
    CHECK(copy.portals().front().targetMap == "field");
    CHECK(copy.ambient().indoor);
    CHECK(copy.ambient().music == "night");
}

TEST_CASE("MapEditor text is loadable by the runtime loader", "[editor]") {
    Tilemap map = makeMap();
    EditorHistory history;
    MapEditor editor(map, history);
    REQUIRE(editor.paint(TileCoord{4, 4}, TileId{kSoilTile}).isOk());

    const auto loaded = fa::tilemap::loadTilemapFromJson(editor.mapText());
    REQUIRE(loaded.isOk());
    CHECK(tileAt(loaded.value(), 4, 4) == TileId{kSoilTile});
}

TEST_CASE("Editor overlay toggles, paints and reports stats", "[editor]") {
    Tilemap map = makeMap();
    EditorOverlay overlay(map);
    overlay.setBrush(TileId{kSoilTile});

    EditorFrameInput input;
    input.cursorWorld = fa::core::Vec2{40.0f, 40.0f};
    input.cursorScreen = fa::core::Vec2{10.0f, 10.0f};
    input.primaryPressed = true;

    overlay.update(input);
    CHECK(tileAt(map, 2, 2) == TileId{kGrassTile});
    CHECK_FALSE(overlay.active());

    overlay.toggle();
    CHECK(overlay.active());
    overlay.update(input);
    CHECK(tileAt(map, 2, 2) == TileId{kSoilTile});
    CHECK(overlay.stats().active);
    CHECK(overlay.stats().undoDepth == 1);
    CHECK(overlay.stats().commands == 1);
    CHECK(overlay.hover() == TileCoord{2, 2});

    overlay.undo();
    CHECK(tileAt(map, 2, 2) == TileId{kGrassTile});
    overlay.redo();
    CHECK(tileAt(map, 2, 2) == TileId{kSoilTile});

    CHECK(overlay.overPanel(fa::core::Vec2{340.0f, 10.0f}));
    CHECK_FALSE(overlay.overPanel(fa::core::Vec2{10.0f, 10.0f}));

    overlay.setTool(EditorTool::Fill);
    overlay.setLayer(3);
    CHECK(overlay.stats().tool == EditorTool::Fill);
    CHECK(overlay.stats().layer == 0);
}

TEST_CASE("Secondary click erases or cancels a rectangle", "[editor]") {
    Tilemap map = makeMap();
    EditorHistory history;
    MapEditor editor(map, history);

    REQUIRE(editor.paint(TileCoord{2, 2}, TileId{kSoilTile}).isOk());
    editor.setHover(TileCoord{2, 2});
    REQUIRE(editor.applySecondary().isOk());
    CHECK(tileAt(map, 2, 2) == fa::tilemap::kEmptyTile);

    editor.setTool(EditorTool::Rect);
    editor.setHover(TileCoord{1, 1});
    REQUIRE(editor.applyPrimary().isOk());
    CHECK(editor.rectActive());
    REQUIRE(editor.applySecondary().isOk());
    CHECK_FALSE(editor.rectActive());
}

TEST_CASE("the object scope places and erases spawn rows without painting tiles", "[editor]") {
    using fa::editor::EditorScope;
    using fa::editor::MapPlacement;
    using fa::editor::PlacementKind;
    using fa::editor::VisualEditor;
    using fa::editor::VisualEditorInput;

    Tilemap map = makeMap();
    const TileId before = tileAt(map, 5, 4);

    VisualEditor editor(map);
    std::vector<MapPlacement> rows;
    editor.setPlacements(&rows, {PlacementKind{"npc", "NPC", {"dialogue.elder"}}, PlacementKind{"bed", "床", {}}});
    editor.setActive(true);
    // An 8x8 map at 16 px tiles fills the viewport exactly, so one tile is one pixel unit.
    const fa::core::Rect viewport{0.0f, 0.0f, 128.0f, 128.0f};

    VisualEditorInput idle;
    idle.cursorScreen = fa::core::Vec2{-100.0f, -100.0f};
    editor.update(idle, viewport);
    editor.setScope(EditorScope::Placements);
    CHECK(editor.scope() == EditorScope::Placements);

    VisualEditorInput click;
    click.cursorScreen = fa::core::Vec2{88.0f, 72.0f};
    click.primaryPressed = true;
    editor.update(click, viewport);

    REQUIRE(editor.placements().size() == 1);
    CHECK(editor.placements()[0].kind == "npc");
    CHECK(editor.placements()[0].cell == TileCoord{5, 4});
    // The first allowed target is preselected, so an NPC is talkable straight away.
    CHECK(editor.placements()[0].target == "dialogue.elder");
    CHECK(editor.dirty());
    CHECK(tileAt(map, 5, 4) == before);

    // A second click on the same cell selects instead of stacking another row.
    editor.update(click, viewport);
    CHECK(editor.placements().size() == 1);

    VisualEditorInput erase;
    erase.cursorScreen = click.cursorScreen;
    erase.secondaryPressed = true;
    editor.update(erase, viewport);
    CHECK(editor.placements().empty());
}

TEST_CASE("the decor scope stacks components on grass without replacing the ground", "[editor]") {
    using fa::editor::EditorScope;
    using fa::editor::VisualEditor;
    using fa::editor::VisualEditorInput;

    Tilemap map = makeMap();
    TileDef flower;
    flower.id = TileId{4};
    flower.name = "flower";
    flower.walkable = true;
    flower.tag = "decor";
    REQUIRE(map.addTileDef(flower));
    // A non-grass cell for the placement rule to reject.
    REQUIRE(map.setTile(2, 2, 0, TileId{kSoilTile}));

    VisualEditor editor(map);
    editor.setActive(true);
    const fa::core::Rect viewport{0.0f, 0.0f, 128.0f, 128.0f};
    VisualEditorInput idle;
    idle.cursorScreen = fa::core::Vec2{-100.0f, -100.0f};
    editor.update(idle, viewport);
    editor.setScope(EditorScope::Decor);

    const auto clickAt = [&](int column, int row, bool secondary) {
        VisualEditorInput in;
        in.cursorScreen =
            fa::core::Vec2{(static_cast<float>(column) + 0.5f) * 16.0f, (static_cast<float>(row) + 0.5f) * 16.0f};
        in.primaryPressed = !secondary;
        in.secondaryPressed = secondary;
        editor.update(in, viewport);
    };

    // The single component definition starts selected: flower stands on top of
    // the grass, and the ground tile keeps its id.
    clickAt(5, 4, false);
    CHECK(map.tileAt(5, 4, 0) == TileId{kGrassTile});
    REQUIRE(map.findLayer("decor") >= 0);  // created on first placement
    const int decor = map.findLayer("decor");
    CHECK(map.tileAt(5, 4, decor) == TileId{4});
    CHECK(editor.dirty());

    // The grass rule rejects everything that is not meadow.
    clickAt(2, 2, false);
    CHECK(map.tileAt(2, 2, decor) == fa::tilemap::kEmptyTile);

    // Right click clears the component, never the ground.
    clickAt(5, 4, true);
    CHECK(map.tileAt(5, 4, decor) == fa::tilemap::kEmptyTile);
    CHECK(map.tileAt(5, 4, 0) == TileId{kGrassTile});

    // Decor edits ride the same undo history as tile brushes.
    REQUIRE(editor.history().undo(map));
    CHECK(map.tileAt(5, 4, decor) == TileId{4});

    // And they serialize into the map document like any layer change.
    const fa::core::Result<Tilemap> reloaded = fa::tilemap::loadTilemapFromJson(editor.mapText());
    REQUIRE(reloaded.isOk());
    const int decorReloaded = reloaded.value().findLayer("decor");
    REQUIRE(decorReloaded >= 0);
    CHECK(reloaded.value().tileAt(5, 4, decorReloaded) == TileId{4});
    CHECK(reloaded.value().tileAt(5, 4, 0) == TileId{kGrassTile});
}

TEST_CASE("the decor scope places entity components as placement rows", "[editor]") {
    using fa::editor::EditorScope;
    using fa::editor::MapPlacement;
    using fa::editor::PlacementKind;
    using fa::editor::VisualEditor;
    using fa::editor::VisualEditorInput;

    Tilemap map = makeMap();
    REQUIRE(map.setTile(2, 2, 0, TileId{kSoilTile}));

    VisualEditor editor(map);
    std::vector<MapPlacement> rows;
    editor.setPlacements(&rows, {PlacementKind{"npc", "NPC", {}}, PlacementKind{"bed", "床", {}}});
    editor.setComponentKinds({PlacementKind{"bed", "床", {}}});
    editor.setActive(true);
    const fa::core::Rect viewport{0.0f, 0.0f, 128.0f, 128.0f};

    VisualEditorInput idle;
    idle.cursorScreen = fa::core::Vec2{-100.0f, -100.0f};
    editor.update(idle, viewport);
    editor.setScope(EditorScope::Decor);

    CHECK_FALSE(editor.selectComponent("enemy"));
    REQUIRE(editor.selectComponent("bed"));

    const auto clickAt = [&](int column, int row, bool secondary) {
        VisualEditorInput in;
        in.cursorScreen =
            fa::core::Vec2{(static_cast<float>(column) + 0.5f) * 16.0f, (static_cast<float>(row) + 0.5f) * 16.0f};
        in.primaryPressed = !secondary;
        in.secondaryPressed = secondary;
        editor.update(in, viewport);
    };

    // Entity components write spawn rows, but every component still needs its
    // ground type: soil is rejected, grass is accepted, and the ground tile is
    // never replaced.
    clickAt(2, 2, false);
    CHECK(editor.placements().empty());
    clickAt(5, 4, false);
    REQUIRE(editor.placements().size() == 1);
    CHECK(editor.placements()[0].kind == "bed");
    CHECK(editor.placements()[0].id == "bed_1");
    CHECK(editor.placements()[0].cell == TileCoord{5, 4});
    CHECK(tileAt(map, 5, 4) == TileId{kGrassTile});
    CHECK(tileAt(map, 2, 2) == TileId{kSoilTile});
    CHECK(editor.dirty());

    // Right click in the same scope removes the row again.
    clickAt(5, 4, true);
    CHECK(editor.placements().empty());
}

TEST_CASE("two clicks with the zone tool in the areas scope create a collision zone that survives JSON", "[editor]") {
    using fa::editor::EditorScope;
    using fa::editor::VisualEditor;
    using fa::editor::VisualEditorInput;

    Tilemap map = makeMap();
    VisualEditor editor(map);
    editor.setActive(true);
    const fa::core::Rect viewport{0.0f, 0.0f, 128.0f, 128.0f};

    VisualEditorInput idle;
    idle.cursorScreen = fa::core::Vec2{-100.0f, -100.0f};
    editor.update(idle, viewport);
    editor.setScope(EditorScope::Areas);
    editor.setAreaTool(VisualEditor::AreaTool::Blocker);  // the collision-zone create tool

    VisualEditorInput first;
    first.cursorScreen = fa::core::Vec2{40.0f, 40.0f};
    first.primaryPressed = true;
    editor.update(first, viewport);
    CHECK(map.blockers().empty());

    VisualEditorInput second;
    second.cursorScreen = fa::core::Vec2{104.0f, 56.0f};
    second.primaryPressed = true;
    editor.update(second, viewport);

    REQUIRE(editor.blockers().size() == 1);
    const fa::tilemap::MapBlocker& zone = map.blockers()[0];
    CHECK(zone.bounds == fa::tilemap::TileBounds{2, 2, 5, 2});
    // Default: the texture exactly covers the box; collision is the box itself.
    CHECK(zone.renderHeight == 2);
    CHECK_FALSE(zone.passable);
    CHECK(map.isSolidCell(3, 3));
    CHECK(editor.dirty());

    const fa::core::Result<Tilemap> reloaded = fa::tilemap::loadTilemapFromJson(editor.mapText());
    REQUIRE(reloaded.isOk());
    REQUIRE(reloaded.value().blockers().size() == 1);
    CHECK(reloaded.value().blockers()[0].bounds == fa::tilemap::TileBounds{2, 2, 5, 2});
    CHECK(reloaded.value().isSolidCell(3, 3));

    // Door mode: the zone keeps its texture but bodies may enter again.
    map.mutableBlockers()[0].passable = true;
    CHECK_FALSE(map.isSolidCell(3, 3));
}

TEST_CASE("the areas scope boxes a farm zone that makes grass tillable", "[editor]") {
    using fa::editor::EditorScope;
    using fa::editor::VisualEditor;
    using fa::editor::VisualEditorInput;

    Tilemap map = makeMap();
    VisualEditor editor(map);
    // The plain grass ground is not tillable before any zone exists.
    CHECK_FALSE(map.isTillable(3, 3));

    editor.setActive(true);
    const fa::core::Rect viewport{0.0f, 0.0f, 128.0f, 128.0f};

    VisualEditorInput idle;
    idle.cursorScreen = fa::core::Vec2{-100.0f, -100.0f};
    editor.update(idle, viewport);
    editor.setScope(EditorScope::Areas);
    editor.setAreaTool(VisualEditor::AreaTool::FarmZone);

    VisualEditorInput first;
    first.cursorScreen = fa::core::Vec2{40.0f, 40.0f};
    first.primaryPressed = true;
    editor.update(first, viewport);
    CHECK(map.areas().empty());

    VisualEditorInput second;
    second.cursorScreen = fa::core::Vec2{104.0f, 56.0f};
    second.primaryPressed = true;
    editor.update(second, viewport);

    REQUIRE(map.areas().size() == 1);
    CHECK(map.areas()[0].kind == fa::tilemap::AreaKind::FarmZone);
    CHECK(map.areas()[0].bounds == fa::tilemap::TileBounds{2, 2, 5, 2});
    // Inside the box the grass may now be hoed; outside it may not.
    CHECK(map.isTillable(3, 3));
    CHECK_FALSE(map.isTillable(0, 0));
    CHECK(editor.dirty());

    const fa::core::Result<Tilemap> reloaded = fa::tilemap::loadTilemapFromJson(editor.mapText());
    REQUIRE(reloaded.isOk());
    REQUIRE(reloaded.value().areas().size() == 1);
    CHECK(reloaded.value().areas()[0].kind == fa::tilemap::AreaKind::FarmZone);
    CHECK(reloaded.value().isTillable(3, 3));
}

TEST_CASE("two clicks in the areas scope create a portal trigger that survives JSON", "[editor]") {
    using fa::editor::EditorScope;
    using fa::editor::VisualEditor;
    using fa::editor::VisualEditorInput;

    Tilemap map = makeMap();
    VisualEditor editor(map);
    editor.setPortalTargets({"farm", "forest"});
    editor.setActive(true);
    const fa::core::Rect viewport{0.0f, 0.0f, 128.0f, 128.0f};

    VisualEditorInput idle;
    idle.cursorScreen = fa::core::Vec2{-100.0f, -100.0f};
    editor.update(idle, viewport);
    editor.setScope(EditorScope::Areas);
    CHECK(editor.currentAreaTool() == VisualEditor::AreaTool::Portal);  // default create tool is the portal

    VisualEditorInput first;
    first.cursorScreen = fa::core::Vec2{40.0f, 40.0f};
    first.primaryPressed = true;
    editor.update(first, viewport);
    CHECK(map.portals().empty());

    VisualEditorInput second;
    second.cursorScreen = fa::core::Vec2{104.0f, 56.0f};
    second.primaryPressed = true;
    editor.update(second, viewport);

    REQUIRE(map.portals().size() == 1);
    const fa::tilemap::MapPortal& portal = map.portals()[0];
    CHECK(portal.bounds == fa::tilemap::TileBounds{2, 2, 5, 2});
    CHECK(portal.targetMap == "farm");
    CHECK(map.portalAt(TileCoord{3, 3}) != nullptr);
    CHECK(editor.dirty());

    // The authored portal has to survive the save round trip the editor performs.
    const fa::core::Result<Tilemap> reloaded = fa::tilemap::loadTilemapFromJson(editor.mapText());
    REQUIRE(reloaded.isOk());
    REQUIRE(reloaded.value().portals().size() == 1);
    CHECK(reloaded.value().portals()[0].bounds == fa::tilemap::TileBounds{2, 2, 5, 2});
    CHECK(reloaded.value().portals()[0].targetMap == "farm");
}
