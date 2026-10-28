/// \file
/// \brief Tests for M12: tile ids and bounds, map construction and queries, layers,
/// areas and portals, the JSON loader, autotiling, the render extraction pass and the
/// physics adapter. Everything is headless - the map produces plain data, so no GL
/// context is needed. See docs/build/m12-tilemap.md for the problems found while
/// writing this.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <fa/core/Math.hpp>
#include <fa/physics/Collider.hpp>
#include <fa/physics/CollisionLayer.hpp>
#include <fa/physics/PhysicsWorld.hpp>
#include <fa/render/RenderQueue.hpp>
#include <fa/scene/Components.hpp>
#include <fa/scene/World.hpp>
#include <fa/tilemap/Autotile.hpp>
#include <fa/tilemap/TileId.hpp>
#include <fa/tilemap/Tilemap.hpp>
#include <fa/tilemap/TilemapError.hpp>
#include <fa/tilemap/TilemapExtract.hpp>
#include <fa/tilemap/TilemapLoader.hpp>

namespace {

using Catch::Approx;
using fa::core::Vec2;
using fa::render::RenderLayer;
using fa::render::TextureHandle;
using fa::tilemap::AreaKind;
using fa::tilemap::AutotileRule;
using fa::tilemap::TileBounds;
using fa::tilemap::TileCoord;
using fa::tilemap::TileDef;
using fa::tilemap::TileExtractOptions;
using fa::tilemap::TileExtractStats;
using fa::tilemap::TileId;
using fa::tilemap::TileLayer;
using fa::tilemap::Tilemap;
using fa::tilemap::TilemapExtractSystem;
using fa::tilemap::TilemapSolids;
using fa::tilemap::TilemapStats;

constexpr int kTile = 16;

TileDef makeDef(std::uint16_t id,
                bool walkable = true,
                bool solid = false,
                bool water = false,
                bool tillable = false,
                std::uint32_t texture = 0) {
    TileDef def;
    def.id = TileId{id};
    def.walkable = walkable;
    def.solid = solid;
    def.water = water;
    def.tillable = tillable;
    if (texture != 0) {
        def.texture = TextureHandle{texture};
    }
    return def;
}

int addLayer(Tilemap& map, std::string name, RenderLayer renderLayer, int zOrder, TileId id, bool visible = true) {
    TileLayer layer;
    layer.name = std::move(name);
    layer.renderLayer = renderLayer;
    layer.zOrder = zOrder;
    layer.visible = visible;
    layer.tiles.assign(map.cellCount(), id);
    return map.addLayer(std::move(layer));
}

/// \brief Builds the most common test map: a ground layer filled with `id`.
void addGround(Tilemap& map, TileId id) {
    REQUIRE(addLayer(map, "ground", RenderLayer::Ground, 0, id) == 0);
}

int loadErrorCode(std::string_view json) {
    const auto result = fa::tilemap::loadTilemapFromJson(json);
    return result.isError() ? result.error().code : 0;
}

}  // namespace

TEST_CASE("Tile ids distinguish empty from defined", "[tilemap][tileid]") {
    REQUIRE_FALSE(fa::tilemap::kEmptyTile.isValid());
    REQUIRE(TileId{0} == fa::tilemap::kEmptyTile);
    REQUIRE(TileId{1}.isValid());
    REQUIRE(TileId{65535}.isValid());
    REQUIRE(static_cast<std::uint16_t>(TileId{42}.value) == 42);
}

TEST_CASE("Tile bounds are half-open", "[tilemap][tileid]") {
    const TileBounds bounds{1, 2, 2, 3};
    REQUIRE(bounds.contains(TileCoord{1, 2}));
    REQUIRE(bounds.contains(TileCoord{2, 4}));
    REQUIRE_FALSE(bounds.contains(TileCoord{3, 4}));
    REQUIRE_FALSE(bounds.contains(TileCoord{1, 5}));
    REQUIRE_FALSE(bounds.contains(TileCoord{0, 2}));
    REQUIRE_FALSE(bounds.isEmpty());
    REQUIRE(TileBounds{}.isEmpty());
}

TEST_CASE("Tilemap rejects unusable dimensions and tile sizes", "[tilemap][map]") {
    Tilemap map;
    REQUIRE(map.empty());
    REQUIRE(map.cellCount() == 0);

    REQUIRE(map.reset(0, 10).isError());
    REQUIRE(map.reset(10, 0).isError());
    REQUIRE(map.reset(Tilemap::kMaxDimension + 1, 10).isError());
    REQUIRE(map.reset(10, 10, 0).isError());
    REQUIRE(map.reset(10, 10, 1025).isError());

    REQUIRE(map.reset(3, 2).isOk());
    REQUIRE(map.width() == 3);
    REQUIRE(map.height() == 2);
    REQUIRE(map.tileSize() == kTile);
    REQUIRE(map.cellCount() == 6);
    REQUIRE_FALSE(map.empty());
    REQUIRE(map.outsideIsSolid());
}

TEST_CASE("Tilemap floors world coordinates into cells", "[tilemap][map]") {
    Tilemap map(10, 10, kTile);

    REQUIRE(map.worldToGrid(Vec2{0.0f, 0.0f}) == TileCoord{0, 0});
    REQUIRE(map.worldToGrid(Vec2{15.9f, 31.9f}) == TileCoord{0, 1});
    REQUIRE(map.worldToGrid(Vec2{16.0f, 32.0f}) == TileCoord{1, 2});
    REQUIRE(map.worldToGrid(Vec2{-0.1f, -16.1f}) == TileCoord{-1, -2});

    map.setOrigin(Vec2{8.0f, 4.0f});
    REQUIRE(map.worldToGrid(Vec2{8.0f, 4.0f}) == TileCoord{0, 0});
    REQUIRE(map.gridToWorld(TileCoord{2, 3}) == Vec2{40.0f, 52.0f});
    REQUIRE(map.tileRect(TileCoord{1, 1}).x == Approx(24.0f));
    REQUIRE(map.tileRect(TileCoord{1, 1}).y == Approx(20.0f));
    REQUIRE(map.boundsRect().w == Approx(160.0f));
    REQUIRE(map.boundsRect().h == Approx(160.0f));
}

TEST_CASE("Tilemap layers need matching sizes and unique names", "[tilemap][map]") {
    Tilemap map(3, 2);

    TileLayer shortLayer;
    shortLayer.name = "ground";
    shortLayer.tiles.assign(5, TileId{1});
    REQUIRE(map.addLayer(std::move(shortLayer)) == -1);

    REQUIRE(addLayer(map, "", RenderLayer::Ground, 0, TileId{1}) == -1);
    REQUIRE(addLayer(map, "ground", RenderLayer::Count, 0, TileId{1}) == -1);
    REQUIRE(addLayer(map, "ground", RenderLayer::Ground, 0, TileId{1}) == 0);
    REQUIRE(addLayer(map, "ground", RenderLayer::Ground, 1, TileId{1}) == -1);
    REQUIRE(addLayer(map, "detail", RenderLayer::GroundDetail, 2, TileId{1}) == 1);

    REQUIRE(map.findLayer("ground") == 0);
    REQUIRE(map.findLayer("detail") == 1);
    REQUIRE(map.findLayer("missing") == -1);
    REQUIRE(map.layerAt(0) != nullptr);
    REQUIRE(map.layerAt(-1) == nullptr);
    REQUIRE(map.layerAt(2) == nullptr);
    REQUIRE(map.layerCount() == 2);
}

TEST_CASE("Tilemap tile access is bounds checked", "[tilemap][map]") {
    Tilemap map(3, 3);
    addGround(map, fa::tilemap::kEmptyTile);
    REQUIRE(map.addTileDef(makeDef(1)));

    REQUIRE_FALSE(map.setTile(-1, 0, 0, TileId{1}));
    REQUIRE_FALSE(map.setTile(3, 0, 0, TileId{1}));
    REQUIRE_FALSE(map.setTile(0, 0, 1, TileId{1}));
    REQUIRE(map.setTile(1, 1, 0, TileId{1}));
    REQUIRE(map.tileAt(1, 1, 0) == TileId{1});
    REQUIRE(map.tileAt(-1, 0, 0) == fa::tilemap::kEmptyTile);
    REQUIRE(map.tileAt(0, 0, 1) == fa::tilemap::kEmptyTile);

    REQUIRE(map.fillLayer(0, TileId{1}));
    REQUIRE(map.tileAt(0, 0, 0) == TileId{1});
    REQUIRE(map.setBorder(0, fa::tilemap::kEmptyTile));

    std::size_t nonEmpty = 0;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            if (map.tileAt(column, row, 0).isValid()) {
                ++nonEmpty;
            }
        }
    }
    REQUIRE(nonEmpty == 1);
    REQUIRE(map.tileAt(1, 1, 0) == TileId{1});
    REQUIRE(map.tileAt(0, 1, 0) == fa::tilemap::kEmptyTile);

    REQUIRE(map.fillLayer(0, fa::tilemap::kEmptyTile));
    REQUIRE(map.setBorder(0, TileId{1}));
    REQUIRE(map.tileAt(1, 1, 0) == fa::tilemap::kEmptyTile);
    REQUIRE(map.tileAt(0, 1, 0) == TileId{1});
}

TEST_CASE("Tilemap combines walkability and solidity across layers", "[tilemap][map]") {
    Tilemap map(3, 1);
    REQUIRE(map.addTileDef(makeDef(1)));                      // grass
    REQUIRE(map.addTileDef(makeDef(2, false, true)));         // wall
    REQUIRE(map.addTileDef(makeDef(3, false, false, true)));  // water
    addGround(map, TileId{1});

    REQUIRE(map.isWalkable(0, 0));
    REQUIRE_FALSE(map.isSolidCell(0, 0));
    REQUIRE_FALSE(map.isWater(0, 0));

    REQUIRE(map.setTile(1, 0, 0, TileId{2}));
    REQUIRE_FALSE(map.isWalkable(1, 0));
    REQUIRE(map.isSolidCell(1, 0));

    REQUIRE(map.setTile(2, 0, 0, TileId{3}));
    REQUIRE_FALSE(map.isWalkable(2, 0));
    REQUIRE_FALSE(map.isSolidCell(2, 0));
    REQUIRE(map.isWater(2, 0));

    REQUIRE(addLayer(map, "detail", RenderLayer::Objects, 1, fa::tilemap::kEmptyTile) == 1);
    REQUIRE(map.setTile(0, 0, 1, TileId{2}));
    REQUIRE(map.isSolidCell(0, 0));

    REQUIRE(map.isSolidCell(-1, 0));
    REQUIRE_FALSE(map.isWalkable(-1, 0));
    map.setOutsideIsSolid(false);
    REQUIRE_FALSE(map.isSolidCell(-1, 0));
    REQUIRE(map.isWalkable(-1, 0));
    REQUIRE_FALSE(map.isWater(-1, 0));
    REQUIRE_FALSE(map.isTillable(-1, 0));
}

TEST_CASE("Tile definitions reject zero and duplicate ids", "[tilemap][map]") {
    Tilemap map(1, 1);
    REQUIRE_FALSE(map.addTileDef(makeDef(0)));
    REQUIRE(map.addTileDef(makeDef(1)));
    REQUIRE_FALSE(map.addTileDef(makeDef(1)));
    REQUIRE(map.tileDefCount() == 1);
    REQUIRE(map.tileDef(TileId{1}) != nullptr);
    REQUIRE(map.tileDef(TileId{2}) == nullptr);
    REQUIRE(map.tileDef(fa::tilemap::kEmptyTile) == nullptr);

    REQUIRE_FALSE(map.setTileTexture(TileId{2}, TextureHandle{7}));
    REQUIRE(map.setTileTexture(TileId{1}, TextureHandle{7}, fa::core::makeRect(4.0f, 8.0f, 16.0f, 16.0f)));
    REQUIRE(map.tileDef(TileId{1})->texture.value() == 7u);
    REQUIRE(map.tileDef(TileId{1})->source.x == Approx(4.0f));
}

TEST_CASE("Map areas resolve by the first containing region", "[tilemap][regions]") {
    Tilemap map(10, 10);
    map.addArea(fa::tilemap::MapArea{1, AreaKind::Greenhouse, TileBounds{1, 1, 3, 3}});
    map.addArea(fa::tilemap::MapArea{2, AreaKind::Outdoor, TileBounds{0, 0, 5, 5}});

    REQUIRE(map.queryArea(TileCoord{1, 1}) == std::optional<AreaKind>{AreaKind::Greenhouse});
    REQUIRE(map.queryArea(TileCoord{4, 4}) == std::optional<AreaKind>{AreaKind::Outdoor});
    REQUIRE_FALSE(map.queryArea(TileCoord{9, 9}).has_value());
    REQUIRE(map.areaAt(TileCoord{9, 9}) == nullptr);
    REQUIRE(fa::tilemap::toString(AreaKind::Greenhouse) == "greenhouse");
    REQUIRE(fa::tilemap::toString(AreaKind::Outdoor) == "outdoor");
}

TEST_CASE("Map portals resolve by their trigger bounds", "[tilemap][regions]") {
    Tilemap map(10, 10);
    fa::tilemap::MapPortal portal;
    portal.id = "farm_to_town";
    portal.bounds = TileBounds{0, 0, 1, 2};
    portal.targetMap = "town";
    portal.targetTile = TileCoord{5, 6};
    map.addPortal(portal);

    REQUIRE(map.portals().size() == 1);
    const fa::tilemap::MapPortal* found = map.portalAt(TileCoord{0, 1});
    REQUIRE(found != nullptr);
    REQUIRE(found->id == "farm_to_town");
    REQUIRE(found->targetMap == "town");
    REQUIRE(found->targetTile == TileCoord{5, 6});
    REQUIRE(map.portalAt(TileCoord{1, 0}) == nullptr);
}

TEST_CASE("JSON loader reads a complete map document", "[tilemap][loader]") {
    constexpr std::string_view kDocument = R"({
      "schema_version": 1,
      "width": 3,
      "height": 2,
      "tile_size": 16,
      "origin": [8, 4],
      "outside_solid": false,
      "tiles": [
        {"id": 1, "name": "grass", "texture": "tex.grass", "source": [0, 0, 16, 16],
         "walkable": true, "solid": false, "tillable": true, "tag": "ground"},
        {"id": 2, "name": "wall", "texture": "tex.wall", "walkable": false, "solid": true},
        {"id": 3, "name": "water", "walkable": false, "water": true}
      ],
      "layers": [
        {"name": "ground", "z_order": 0, "render_layer": "ground", "tiles": [1, 1, 1, 2, 1, 3]},
        {"name": "detail", "z_order": 5, "render_layer": "ground_detail",
         "visible": false, "tiles": [0, 0, 0, 0, 0, 0]}
      ],
      "areas": [
        {"id": 1, "kind": "greenhouse", "bounds": [0, 0, 2, 1]}
      ],
      "portals": [
        {"id": "to_town", "bounds": [2, 1, 1, 1], "target_map": "town", "target_tile": [5, 6]}
      ],
      "ambient": {"indoor": true, "light": [0.5, 0.5, 0.5, 1.0], "music": "greenhouse"}
    })";

    const auto loaded = fa::tilemap::loadTilemapFromJson(kDocument);
    REQUIRE(loaded.isOk());
    const Tilemap& map = loaded.value();

    REQUIRE(map.width() == 3);
    REQUIRE(map.height() == 2);
    REQUIRE(map.tileSize() == 16);
    REQUIRE(map.origin() == Vec2{8.0f, 4.0f});
    REQUIRE_FALSE(map.outsideIsSolid());
    REQUIRE(map.tileDefCount() == 3);

    const TileDef* grass = map.tileDef(TileId{1});
    REQUIRE(grass != nullptr);
    REQUIRE(grass->name == "grass");
    REQUIRE(grass->textureName == "tex.grass");
    REQUIRE_FALSE(grass->texture.isValid());
    REQUIRE(grass->source.w == Approx(16.0f));
    REQUIRE(grass->walkable);
    REQUIRE_FALSE(grass->solid);
    REQUIRE(grass->tillable);
    REQUIRE(grass->tag == "ground");

    const TileDef* wall = map.tileDef(TileId{2});
    REQUIRE(wall != nullptr);
    REQUIRE_FALSE(wall->walkable);
    REQUIRE(wall->solid);

    REQUIRE(map.layerCount() == 2);
    REQUIRE(map.findLayer("ground") == 0);
    REQUIRE(map.findLayer("detail") == 1);
    REQUIRE(map.layerAt(1)->visible == false);
    REQUIRE(map.layerAt(1)->renderLayer == RenderLayer::GroundDetail);
    REQUIRE(map.layerAt(1)->zOrder == 5);

    REQUIRE(map.tileAt(0, 1, 0) == TileId{2});
    REQUIRE(map.tileAt(1, 1, 0) == TileId{1});
    REQUIRE(map.tileAt(2, 1, 0) == TileId{3});
    REQUIRE(map.isSolidCell(0, 1));
    REQUIRE_FALSE(map.isWalkable(0, 1));
    REQUIRE(map.isWater(2, 1));
    REQUIRE(map.isTillable(1, 0));
    REQUIRE_FALSE(map.isSolidCell(-1, 0));

    REQUIRE(map.areas().size() == 1);
    REQUIRE(map.queryArea(TileCoord{0, 0}) == std::optional<AreaKind>{AreaKind::Greenhouse});
    REQUIRE_FALSE(map.queryArea(TileCoord{2, 0}).has_value());

    REQUIRE(map.portals().size() == 1);
    const fa::tilemap::MapPortal* portal = map.portalAt(TileCoord{2, 1});
    REQUIRE(portal != nullptr);
    REQUIRE(portal->targetMap == "town");

    REQUIRE(map.ambient().indoor);
    REQUIRE(map.ambient().music == "greenhouse");
    REQUIRE(map.ambient().light.r == Approx(0.5f));

    const TilemapStats stats = map.stats();
    REQUIRE(stats.layers == 2);
    REQUIRE(stats.tileDefs == 3);
    REQUIRE(stats.areas == 1);
    REQUIRE(stats.portals == 1);
    REQUIRE(stats.nonEmptyTiles == 6);
    REQUIRE(stats.solidTiles == 1);
}

TEST_CASE("JSON loader rejects malformed and inconsistent documents", "[tilemap][loader]") {
    using namespace fa::tilemap::tilemap_error;

    REQUIRE(loadErrorCode("{ not json") == kParseFailed);
    REQUIRE(loadErrorCode("[]") == kInvalidDocument);
    REQUIRE(loadErrorCode(R"({"width": 2, "height": 2, "layers": []})") == kInvalidDocument);
    REQUIRE(loadErrorCode(R"({"schema_version": 2, "width": 2, "height": 2, "layers": []})") == kUnsupportedSchema);
    REQUIRE(loadErrorCode(R"({"schema_version": 1, "width": 0, "height": 2, "layers": []})") == kInvalidDimensions);
    REQUIRE(loadErrorCode(R"({"schema_version": 1, "width": 2, "height": 2, "tile_size": 0,
                             "layers": [{"name": "g", "tiles": [0, 0, 0, 0]}]})") == kInvalidTileSize);
    REQUIRE(loadErrorCode(R"({"schema_version": 1, "width": 2, "height": 2,
                             "layers": [{"name": "g", "tiles": [0, 0, 0]}]})") == kLayerSizeMismatch);
    REQUIRE(loadErrorCode(R"({"schema_version": 1, "width": 2, "height": 2,
                             "layers": [{"name": "g", "tiles": [1, 0, 0, 0]}]})") == kUnknownTileId);
    REQUIRE(loadErrorCode(R"({"schema_version": 1, "width": 2, "height": 2,
                             "tiles": [{"id": 1}, {"id": 1}],
                             "layers": [{"name": "g", "tiles": [0, 0, 0, 0]}]})") == kDuplicateTileId);
    REQUIRE(loadErrorCode(R"({"schema_version": 1, "width": 2, "height": 2,
                             "layers": [{"name": "g", "render_layer": "sky", "tiles": [0, 0, 0, 0]}]})") ==
            kInvalidLayer);
    REQUIRE(loadErrorCode(R"({"schema_version": 1, "width": 2, "height": 2,
                             "layers": [{"name": "g", "tiles": [0, 0, 0, 0]},
                                        {"name": "g", "tiles": [0, 0, 0, 0]}]})") == kInvalidLayer);
    REQUIRE(loadErrorCode(R"({"schema_version": 1, "width": 2, "height": 2,
                             "layers": [{"name": "g", "tiles": [0, 0, 0, 0]}],
                             "areas": [{"id": 1, "kind": "greenhouse", "bounds": [1, 1, 4, 4]}]})") == kInvalidRegion);
    REQUIRE(loadErrorCode(R"({"schema_version": 1, "width": 2, "height": 2,
                             "layers": [{"name": "g", "tiles": [0, 0, 0, 0]}],
                             "portals": [{"id": "p", "bounds": [0, 0, 1, 1], "target_tile": [1, 1]}]})") ==
            kInvalidRegion);
    REQUIRE(loadErrorCode(R"({"schema_version": 1, "width": 2, "height": 2, "outside_solid": "yes",
                             "layers": [{"name": "g", "tiles": [0, 0, 0, 0]}]})") == kInvalidDocument);
    REQUIRE(loadErrorCode(R"({"schema_version": 1, "width": 2, "height": 2,
                             "layers": [{"name": "g"}]})") == kInvalidLayer);
}

TEST_CASE("Autotile masks count matching orthogonal neighbours", "[tilemap][autotile]") {
    Tilemap map(3, 3, kTile);
    REQUIRE(map.addTileDef(makeDef(1)));
    addGround(map, TileId{1});

    REQUIRE(fa::tilemap::neighborMask(map, 1, 1, 0, TileId{1}) == 15);
    REQUIRE(fa::tilemap::neighborMask(map, 1, 1, 0, TileId{9}) == 0);

    REQUIRE(map.setTile(2, 1, 0, fa::tilemap::kEmptyTile));
    REQUIRE(fa::tilemap::neighborMask(map, 1, 1, 0, TileId{1}) == 13);
    REQUIRE(map.setTile(1, 0, 0, fa::tilemap::kEmptyTile));
    REQUIRE(fa::tilemap::neighborMask(map, 1, 1, 0, TileId{1}) == 12);
}

TEST_CASE("Autotile baking uses a snapshot of the layer", "[tilemap][autotile]") {
    Tilemap map(5, 5, kTile);
    REQUIRE(map.addTileDef(makeDef(1)));
    REQUIRE(map.addTileDef(makeDef(2)));
    REQUIRE(map.addTileDef(makeDef(10)));
    REQUIRE(map.addTileDef(makeDef(11)));
    addGround(map, TileId{2});
    for (int row = 1; row <= 3; ++row) {
        for (int column = 1; column <= 3; ++column) {
            REQUIRE(map.setTile(column, row, 0, TileId{1}));
        }
    }

    AutotileRule rule;
    rule.base = TileId{1};
    rule.variants[15] = TileId{10};
    rule.variants[14] = TileId{11};

    REQUIRE(rule.variantFor(15) == TileId{10});
    REQUIRE(rule.variantFor(14) == TileId{11});
    REQUIRE(rule.variantFor(0) == TileId{1});
    REQUIRE(rule.variantFor(200) == TileId{1});

    const std::size_t changed = fa::tilemap::bakeAutotiles(map, 0, rule);
    REQUIRE(changed == 2);
    REQUIRE(map.tileAt(2, 2, 0) == TileId{10});
    REQUIRE(map.tileAt(2, 1, 0) == TileId{11});
    REQUIRE(map.tileAt(1, 2, 0) == TileId{1});
}

TEST_CASE("Tile extraction submits one item per drawable tile", "[tilemap][extract]") {
    Tilemap map(2, 2, kTile);
    REQUIRE(map.addTileDef(makeDef(1, true, false, false, false, 5)));
    REQUIRE(map.addTileDef(makeDef(2, false, true, false, false, 6)));
    addGround(map, TileId{1});

    fa::render::RenderQueue queue;
    const TileExtractStats stats = fa::tilemap::extractTiles(map, queue);
    REQUIRE(stats.layers == 1);
    REQUIRE(stats.candidates == 4);
    REQUIRE(stats.culled == 0);
    REQUIRE(stats.skippedEmpty == 0);
    REQUIRE(stats.skippedNoTexture == 0);
    REQUIRE(stats.submitted == 4);
    REQUIRE(queue.size() == 4);

    const auto items = queue.items();
    REQUIRE(items[0].texture.value() == 5u);
    REQUIRE(items[0].position == Vec2{0.0f, 0.0f});
    REQUIRE(items[0].size == Vec2{16.0f, 16.0f});
    REQUIRE(items[0].layer == RenderLayer::Ground);
    REQUIRE(items[0].sortY == Approx(16.0f));
    REQUIRE(items[0].entityId != 0u);
    REQUIRE(items[3].position == Vec2{16.0f, 16.0f});

    REQUIRE(map.setTile(1, 0, 0, fa::tilemap::kEmptyTile));
    queue.clear();
    REQUIRE(fa::tilemap::extractTiles(map, queue).skippedEmpty == 1);
    REQUIRE(queue.size() == 3);

    REQUIRE(map.setTile(1, 0, 0, TileId{2}));
    REQUIRE(map.setTileTexture(TileId{2}, TextureHandle{}));
    queue.clear();
    REQUIRE(fa::tilemap::extractTiles(map, queue).skippedNoTexture == 1);
    REQUIRE(queue.size() == 3);

    TileExtractOptions keepUnbound;
    keepUnbound.skipWithoutTexture = false;
    queue.clear();
    REQUIRE(fa::tilemap::extractTiles(map, queue, keepUnbound).submitted == 4);
    REQUIRE(queue.size() == 4);
}

TEST_CASE("Tile extraction honours layer order, culling and visibility", "[tilemap][extract]") {
    Tilemap map(4, 4, kTile);
    REQUIRE(map.addTileDef(makeDef(1, true, false, false, false, 5)));
    REQUIRE(map.addTileDef(makeDef(2, true, false, false, false, 6)));
    REQUIRE(addLayer(map, "detail", RenderLayer::GroundDetail, -1, TileId{2}) == 0);
    REQUIRE(addLayer(map, "ground", RenderLayer::Ground, 10, TileId{1}) == 1);

    fa::render::RenderQueue queue;
    TileExtractStats stats = fa::tilemap::extractTiles(map, queue);
    REQUIRE(stats.layers == 2);
    REQUIRE(stats.submitted == 32);
    REQUIRE(queue.items()[0].texture.value() == 6u);
    REQUIRE(queue.items()[16].texture.value() == 5u);

    queue.clear();
    TileExtractOptions culled;
    culled.cullToViewBounds = true;
    culled.viewBounds = fa::core::makeRect(16.0f, 16.0f, 32.0f, 32.0f);
    stats = fa::tilemap::extractTiles(map, queue, culled);
    REQUIRE(stats.candidates == 8);
    REQUIRE(stats.submitted == 8);
    REQUIRE(stats.culled == 24);
    REQUIRE(queue.size() == 8);

    queue.clear();
    culled.viewBounds = fa::core::makeRect(-100.0f, -100.0f, 10.0f, 10.0f);
    stats = fa::tilemap::extractTiles(map, queue, culled);
    REQUIRE(stats.candidates == 0);
    REQUIRE(stats.submitted == 0);
    REQUIRE(stats.culled == 32);

    map.layerAt(0)->visible = false;
    queue.clear();
    stats = fa::tilemap::extractTiles(map, queue);
    REQUIRE(stats.layers == 1);
    REQUIRE(stats.submitted == 16);
    REQUIRE(queue.size() == 16);

    TilemapExtractSystem system(map);
    system.setViewBounds(fa::core::makeRect(0.0f, 0.0f, 16.0f, 16.0f));
    REQUIRE(system.options().cullToViewBounds);
    fa::scene::World world;
    fa::render::RenderQueue systemQueue;
    system.onRenderExtract(world, systemQueue);
    REQUIRE(system.name() == "tilemap.extract");
    REQUIRE(system.lastStats().submitted == 1);
    REQUIRE(systemQueue.size() == 1);

    system.clearViewBounds();
    REQUIRE_FALSE(system.options().cullToViewBounds);
}

TEST_CASE("TilemapSolids bridges the map to physics", "[tilemap][physics]") {
    Tilemap map(4, 4, kTile);
    REQUIRE(map.addTileDef(makeDef(1)));
    REQUIRE(map.addTileDef(makeDef(2, false, true)));
    addGround(map, TileId{1});
    REQUIRE(map.setTile(2, 1, 0, TileId{2}));

    TilemapSolids solids{map};
    REQUIRE(solids.tilemap() == &map);
    REQUIRE(solids.tileSize() == Approx(16.0f));
    REQUIRE(solids.columns() == 4);
    REQUIRE(solids.rows() == 4);
    REQUIRE(solids.isSolid(2, 1));
    REQUIRE_FALSE(solids.isSolid(0, 0));
    REQUIRE(solids.isSolid(-1, 0));

    map.setOutsideIsSolid(false);
    REQUIRE_FALSE(solids.isSolid(-1, 0));

    TilemapSolids none;
    REQUIRE(none.tileSize() == Approx(16.0f));
    REQUIRE(none.columns() == 0);
    REQUIRE(none.rows() == 0);
    REQUIRE(none.isSolid(0, 0));

    fa::scene::World world;
    const fa::scene::EntityId id = world.create();
    fa::scene::Transform transform;
    transform.position = Vec2{10.0f, 16.0f};
    static_cast<void>(world.add<fa::scene::Transform>(id, transform));

    fa::physics::Collider collider;
    collider.size = Vec2{8.0f, 8.0f};
    collider.layer = fa::physics::CollisionLayer::Player;
    collider.mask = fa::physics::maskOf(fa::physics::CollisionLayer::World);
    static_cast<void>(world.add<fa::physics::Collider>(id, collider));

    fa::physics::PhysicsWorld physics;
    physics.setTiles(&solids);
    const fa::physics::MoveResult moved = physics.move(world, id, Vec2{40.0f, 0.0f});
    REQUIRE(moved.hitX);
    const fa::scene::Transform* finalTransform = world.tryGet<fa::scene::Transform>(id);
    REQUIRE(finalTransform != nullptr);
    REQUIRE(finalTransform->position.x == Approx(24.0f));
}
