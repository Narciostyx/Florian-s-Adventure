/// \file
/// \brief Tests for M26: undo/redo, brush/rect/fill/erase/picker, JSON round-trip,
/// overlay activation/input and the 50-step undo acceptance case.

#include <catch2/catch_test_macros.hpp>

#include "fa/editor/EditorHistory.hpp"
#include "fa/editor/EditorOverlay.hpp"
#include "fa/editor/MapEditor.hpp"
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
