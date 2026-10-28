/// \file
/// \brief Tests for M19: tilling, watering, planting, daily growth, seasons,
/// greenhouse bypass, harvest/regrow, world objects, debris and the M17 adapter.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string_view>
#include <vector>

#include "fa/content/ContentDatabase.hpp"
#include "fa/farming/FarmingService.hpp"
#include "fa/farming/FarmingTools.hpp"
#include "fa/farming/FarmingWorld.hpp"
#include "fa/interaction/ToolHandler.hpp"
#include "fa/inventory/Inventory.hpp"
#include "fa/scene/World.hpp"
#include "fa/tilemap/Tilemap.hpp"

namespace {

using Catch::Approx;

using fa::content::ContentDatabase;
using fa::content::ContentId;
using fa::content::Season;
using fa::farming::Debris;
using fa::farming::DebrisKind;
using fa::farming::DebrisLayer;
using fa::farming::FarmingDayContext;
using fa::farming::FarmingEventKind;
using fa::farming::FarmingService;
using fa::farming::FarmingToolHandler;
using fa::farming::WorldObject;
using fa::farming::WorldObjectKind;
using fa::farming::WorldObjectLayer;
using fa::inventory::Inventory;
using fa::tilemap::AreaKind;
using fa::tilemap::MapArea;
using fa::tilemap::TileCoord;
using fa::tilemap::TileDef;
using fa::tilemap::TileId;
using fa::tilemap::TileLayer;
using fa::tilemap::Tilemap;

constexpr std::uint16_t kGrassTile = 1;
constexpr std::uint16_t kSoilTile = 2;

constexpr std::string_view kContentText = R"({
  "schema_version": 1,
  "items": [
    {"id": "item.seed", "display_name": "Seed", "category": "seed",
     "max_stack": 99, "sell_price": 1},
    {"id": "item.seed2", "display_name": "Regrow Seed", "category": "seed",
     "max_stack": 99, "sell_price": 1},
    {"id": "item.crop", "display_name": "Crop", "category": "crop",
     "max_stack": 99, "sell_price": 10},
    {"id": "item.weed", "display_name": "Weed", "category": "material",
     "max_stack": 99, "sell_price": 1}
  ],
  "crops": [
    {"id": "crop.test", "seed_item": "item.seed", "harvest_item": "item.crop",
     "stage_days": [1, 2], "seasons": ["spring"], "regrow": false, "regrow_days": 0},
    {"id": "crop.regrow", "seed_item": "item.seed2", "harvest_item": "item.crop",
     "stage_days": [1, 1], "seasons": ["any"], "regrow": true, "regrow_days": 1}
  ]
})";

ContentDatabase makeContent() {
    ContentDatabase content;
    REQUIRE(content.loadDocument(kContentText).isOk());
    return content;
}

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

    TileLayer layer;
    layer.name = "ground";
    layer.tiles.assign(map.cellCount(), TileId{kGrassTile});
    REQUIRE(map.addLayer(layer) >= 0);
    REQUIRE(map.setTile(1, 1, 0, TileId{kSoilTile}));
    REQUIRE(map.setTile(2, 1, 0, TileId{kSoilTile}));
    REQUIRE(map.setTile(3, 1, 0, TileId{kSoilTile}));
    REQUIRE(map.setTile(4, 1, 0, TileId{kSoilTile}));

    MapArea greenhouse;
    greenhouse.areaId = 1;
    greenhouse.kind = AreaKind::Greenhouse;
    greenhouse.bounds = fa::tilemap::TileBounds{4, 1, 1, 1};
    map.addArea(greenhouse);
    return map;
}

FarmingDayContext springDay(bool rain = false, float multiplier = 1.0f) {
    return FarmingDayContext{Season::Spring, rain, multiplier};
}

}  // namespace

TEST_CASE("Farming tills only tillable cells", "[farming]") {
    const Tilemap map = makeMap();
    FarmingService farming;

    const TileCoord soil{1, 1};
    CHECK(farming.till(soil, map).isOk());
    CHECK(farming.tileAt(soil) != nullptr);
    CHECK(farming.tileAt(soil)->tilled);
    CHECK(farming.stats().tilled == 1);
    CHECK(farming.stats().tills == 1);

    CHECK(farming.till(soil, map).error().code == fa::farming::farming_error::kAlreadyTilled);
    CHECK(farming.till(TileCoord{0, 0}, map).error().code == fa::farming::farming_error::kNotTillable);
    CHECK(farming.till(TileCoord{100, 100}, map).error().code == fa::farming::farming_error::kNotTillable);
    CHECK(farming.events().size() == 1);
    CHECK(farming.events().front().kind == FarmingEventKind::Tilled);
}

TEST_CASE("Farming waters tilled cells and dries them daily", "[farming]") {
    const Tilemap map = makeMap();
    FarmingService farming;
    const TileCoord cell{1, 1};

    CHECK(farming.water(cell).error().code == fa::farming::farming_error::kNotTilled);
    REQUIRE(farming.till(cell, map).isOk());
    CHECK(farming.water(cell).isOk());
    CHECK(farming.stats().watered == 1);
    CHECK(farming.stats().waters == 1);

    farming.onDayChanged(springDay());
    CHECK_FALSE(farming.tileAt(cell)->watered);
    farming.onDayChanged(springDay());
    CHECK(farming.tileAt(cell)->daysSinceWatered == 1);
}

TEST_CASE("Farming plants seeds and checks seasons", "[farming]") {
    const Tilemap map = makeMap();
    const ContentDatabase content = makeContent();
    FarmingService farming(&content);
    const TileCoord cell{1, 1};

    REQUIRE(farming.till(cell, map).isOk());
    CHECK(farming.plant(cell, ContentId::fromName("item.unknown")).error().code ==
          fa::farming::farming_error::kUnknownSeed);
    REQUIRE(farming.plant(cell, ContentId::fromName("item.seed")).isOk());
    CHECK(farming.cropAt(cell) != nullptr);
    CHECK(farming.cropAt(cell)->crop == ContentId::fromName("crop.test"));
    CHECK(farming.stats().crops == 1);
    CHECK(farming.plant(cell, ContentId::fromName("item.seed")).error().code ==
          fa::farming::farming_error::kAlreadyPlanted);

    FarmingService winter(&content);
    REQUIRE(winter.till(TileCoord{2, 1}, map).isOk());
    winter.setCurrentSeason(Season::Winter);
    CHECK(winter.plant(TileCoord{2, 1}, ContentId::fromName("item.seed")).error().code ==
          fa::farming::farming_error::kWrongSeason);
}

TEST_CASE("Greenhouse cells ignore the season mask", "[farming]") {
    const Tilemap map = makeMap();
    const ContentDatabase content = makeContent();
    FarmingService farming(&content);
    farming.setCurrentSeason(Season::Winter);

    const TileCoord greenhouse{4, 1};
    REQUIRE(farming.till(greenhouse, map).isOk());
    CHECK(farming.tileAt(greenhouse)->greenhouse);
    CHECK(farming.plant(greenhouse, ContentId::fromName("item.seed")).isOk());

    farming.onDayChanged(FarmingDayContext{Season::Winter, false, 1.0f});
    CHECK(farming.cropAt(greenhouse) != nullptr);
    CHECK_FALSE(farming.cropAt(greenhouse)->dead);
}

TEST_CASE("Crops grow only on watered days", "[farming]") {
    const Tilemap map = makeMap();
    const ContentDatabase content = makeContent();
    FarmingService farming(&content);
    const TileCoord cell{1, 1};

    REQUIRE(farming.till(cell, map).isOk());
    REQUIRE(farming.plant(cell, ContentId::fromName("item.seed")).isOk());

    farming.onDayChanged(springDay());
    CHECK(farming.cropAt(cell)->growthDays == Approx(0.0f));
    CHECK(farming.cropAt(cell)->stage == 0);

    REQUIRE(farming.water(cell).isOk());
    farming.onDayChanged(springDay());
    CHECK(farming.cropAt(cell)->growthDays == Approx(1.0f));
    CHECK(farming.cropAt(cell)->stage == 1);
    CHECK_FALSE(farming.cropAt(cell)->harvestable);

    REQUIRE(farming.water(cell).isOk());
    farming.onDayChanged(springDay());
    CHECK(farming.cropAt(cell)->growthDays == Approx(2.0f));
    CHECK(farming.cropAt(cell)->stage == 1);

    REQUIRE(farming.water(cell).isOk());
    farming.onDayChanged(springDay());
    CHECK(farming.cropAt(cell)->growthDays == Approx(3.0f));
    CHECK(farming.cropAt(cell)->stage == 2);
    CHECK(farming.cropAt(cell)->harvestable);
}

TEST_CASE("Rain auto-waters and weather scales growth", "[farming]") {
    const Tilemap map = makeMap();
    const ContentDatabase content = makeContent();
    FarmingService farming(&content);
    const TileCoord cell{1, 1};

    REQUIRE(farming.till(cell, map).isOk());
    REQUIRE(farming.plant(cell, ContentId::fromName("item.seed")).isOk());
    farming.onDayChanged(springDay(true, 1.5f));

    CHECK(farming.cropAt(cell)->growthDays == Approx(1.5f));
    CHECK(farming.cropAt(cell)->wateredToday);
    CHECK_FALSE(farming.tileAt(cell)->watered);
}

TEST_CASE("Harvest moves the item into the inventory", "[farming]") {
    const Tilemap map = makeMap();
    const ContentDatabase content = makeContent();
    FarmingService farming(&content);
    Inventory inventory(2, 99, &content);
    const TileCoord cell{1, 1};

    REQUIRE(farming.till(cell, map).isOk());
    REQUIRE(farming.plant(cell, ContentId::fromName("item.seed")).isOk());
    for (int day = 0; day < 3; ++day) {
        REQUIRE(farming.water(cell).isOk());
        farming.onDayChanged(springDay());
    }
    REQUIRE(farming.cropAt(cell)->harvestable);

    const auto harvested = farming.harvest(cell, inventory);
    REQUIRE(harvested.isOk());
    CHECK(harvested.value().id == ContentId::fromName("item.crop"));
    CHECK(harvested.value().count == 1);
    CHECK(inventory.countOf(ContentId::fromName("item.crop")) == 1);
    CHECK(farming.cropAt(cell) == nullptr);
    CHECK(farming.tileAt(cell)->tilled);
    CHECK(farming.stats().harvests == 1);
    CHECK(farming.harvest(cell, inventory).error().code == fa::farming::farming_error::kNotHarvestable);
}

TEST_CASE("Regrowing crops stay planted and mature again", "[farming]") {
    const Tilemap map = makeMap();
    const ContentDatabase content = makeContent();
    FarmingService farming(&content);
    Inventory inventory(2, 99, &content);
    const TileCoord cell{1, 1};

    REQUIRE(farming.till(cell, map).isOk());
    REQUIRE(farming.plant(cell, ContentId::fromName("item.seed2")).isOk());
    for (int day = 0; day < 2; ++day) {
        REQUIRE(farming.water(cell).isOk());
        farming.onDayChanged(springDay());
    }
    REQUIRE(farming.cropAt(cell)->harvestable);
    REQUIRE(farming.harvest(cell, inventory).isOk());
    REQUIRE(farming.cropAt(cell) != nullptr);
    CHECK_FALSE(farming.cropAt(cell)->harvestable);
    CHECK(farming.cropAt(cell)->growthDays == Approx(1.0f));

    REQUIRE(farming.water(cell).isOk());
    farming.onDayChanged(springDay());
    CHECK(farming.cropAt(cell)->harvestable);
}

TEST_CASE("Season change wilts crops outside the greenhouse", "[farming]") {
    const Tilemap map = makeMap();
    const ContentDatabase content = makeContent();
    FarmingService farming(&content);
    const TileCoord cell{1, 1};

    REQUIRE(farming.till(cell, map).isOk());
    REQUIRE(farming.plant(cell, ContentId::fromName("item.seed")).isOk());
    farming.onDayChanged(FarmingDayContext{Season::Winter, false, 1.0f});
    REQUIRE(farming.cropAt(cell) != nullptr);
    CHECK(farming.cropAt(cell)->dead);
    CHECK(farming.stats().wilted == 1);

    Inventory inventory(2, 99, &content);
    CHECK(farming.harvest(cell, inventory).error().code == fa::farming::farming_error::kDeadCrop);
}

TEST_CASE("World objects occupy one cell at a time", "[farming]") {
    WorldObjectLayer objects;
    WorldObject chest;
    chest.def = ContentId::fromName("item.chest");
    chest.kind = WorldObjectKind::Chest;
    chest.cell = TileCoord{3, 3};

    CHECK(objects.add(chest));
    CHECK(objects.occupied(TileCoord{3, 3}));
    CHECK(objects.at(TileCoord{3, 3}) != nullptr);
    CHECK(objects.size() == 1);
    CHECK_FALSE(objects.add(chest));
    CHECK(objects.removeAt(TileCoord{3, 3}));
    CHECK_FALSE(objects.removeAt(TileCoord{3, 3}));
    CHECK(objects.size() == 0);
}

TEST_CASE("Debris clears once and keeps its yield", "[farming]") {
    DebrisLayer debris;
    Debris weed;
    weed.cell = TileCoord{2, 2};
    weed.kind = DebrisKind::Weed;
    weed.yield = ContentId::fromName("item.weed");
    weed.amount = 2;

    CHECK(debris.add(weed));
    CHECK(debris.occupied(TileCoord{2, 2}));
    CHECK(debris.at(TileCoord{2, 2})->amount == 2);
    CHECK_FALSE(debris.add(weed));
    CHECK(debris.removeAt(TileCoord{2, 2}));
    CHECK(debris.size() == 0);
}

TEST_CASE("Farming tool handler drives the service", "[farming][tool]") {
    const Tilemap map = makeMap();
    const ContentDatabase content = makeContent();
    FarmingService farming(&content);
    Inventory inventory(4, 99, &content);
    FarmingToolHandler handler(farming, inventory);
    DebrisLayer debris;
    handler.setDebrisLayer(&debris);
    handler.setSelectedSeed(ContentId::fromName("item.seed"));

    REQUIRE(inventory.add(fa::inventory::ItemStack{ContentId::fromName("item.seed"), 5, -1}).isOk());
    fa::scene::World world;

    const auto use = [&](TileCoord cell, fa::interaction::ToolKind tool) {
        fa::interaction::InteractionRequest request;
        request.tool = tool;
        request.target.cell = cell;
        request.target.tile = map.tileAt(cell.x, cell.y, 0);
        request.target.valid = true;
        request.target.reachable = true;
        const fa::interaction::ToolUseContext context{world, map, nullptr, request};
        return handler.use(context);
    };

    const TileCoord cell{1, 1};
    CHECK(handler.handles(fa::interaction::ToolKind::Hoe));
    CHECK(handler.handles(fa::interaction::ToolKind::Harvest));
    CHECK_FALSE(handler.handles(fa::interaction::ToolKind::Sword));
    CHECK(use(cell, fa::interaction::ToolKind::Hoe).isOk());
    CHECK(use(cell, fa::interaction::ToolKind::WateringCan).isOk());
    CHECK(use(cell, fa::interaction::ToolKind::Seed).isOk());
    CHECK(inventory.countOf(ContentId::fromName("item.seed")) == 4);

    for (int day = 0; day < 3; ++day) {
        CHECK(use(cell, fa::interaction::ToolKind::WateringCan).isOk());
        farming.onDayChanged(springDay());
    }
    CHECK(use(cell, fa::interaction::ToolKind::Harvest).isOk());
    CHECK(inventory.countOf(ContentId::fromName("item.crop")) == 1);

    Debris weed;
    weed.cell = TileCoord{2, 2};
    weed.yield = ContentId::fromName("item.weed");
    weed.amount = 2;
    REQUIRE(debris.add(weed));
    CHECK(use(TileCoord{2, 2}, fa::interaction::ToolKind::Axe).isOk());
    CHECK(inventory.countOf(ContentId::fromName("item.weed")) == 2);
    CHECK(debris.size() == 0);

    const auto stats = handler.stats();
    CHECK(stats.hoeUses == 1);
    CHECK(stats.seedUses == 1);
    CHECK(stats.harvestUses == 1);
    CHECK(stats.harvestItems == 1);
    CHECK(stats.clearUses == 1);
}

TEST_CASE("Farming tool handler can rebind its inventory", "[farming][tool]") {
    const Tilemap map = makeMap();
    const ContentDatabase content = makeContent();
    FarmingService farming(&content);
    Inventory first(4, 99, &content);
    Inventory second(4, 99, &content);
    FarmingToolHandler handler(farming, first);
    handler.setSelectedSeed(ContentId::fromName("item.seed"));
    REQUIRE(first.add(fa::inventory::ItemStack{ContentId::fromName("item.seed"), 5, -1}).isOk());

    fa::scene::World world;
    const auto use = [&](TileCoord cell, fa::interaction::ToolKind tool) {
        fa::interaction::InteractionRequest request;
        request.tool = tool;
        request.target.cell = cell;
        request.target.tile = map.tileAt(cell.x, cell.y, 0);
        request.target.valid = true;
        request.target.reachable = true;
        const fa::interaction::ToolUseContext context{world, map, nullptr, request};
        return handler.use(context);
    };

    const TileCoord cell{1, 1};
    CHECK(use(cell, fa::interaction::ToolKind::Hoe).isOk());
    CHECK(use(cell, fa::interaction::ToolKind::Seed).isOk());
    CHECK(first.countOf(ContentId::fromName("item.seed")) == 4);

    handler.setInventory(second);
    for (int day = 0; day < 3; ++day) {
        CHECK(use(cell, fa::interaction::ToolKind::WateringCan).isOk());
        farming.onDayChanged(springDay());
    }
    CHECK(use(cell, fa::interaction::ToolKind::Harvest).isOk());
    CHECK(second.countOf(ContentId::fromName("item.crop")) == 1);
    CHECK(first.countOf(ContentId::fromName("item.crop")) == 0);
}

TEST_CASE("Farming restore replaces the persisted field state", "[farming]") {
    const Tilemap map = makeMap();
    const ContentDatabase content = makeContent();
    FarmingService farming(&content);
    const TileCoord cell{1, 1};

    REQUIRE(farming.till(cell, map).isOk());
    REQUIRE(farming.plant(cell, ContentId::fromName("item.seed")).isOk());
    REQUIRE(farming.water(cell).isOk());
    farming.onDayChanged(springDay());
    REQUIRE(farming.cropAt(cell) != nullptr);

    const std::vector<fa::farming::FarmTileEntry> snapshot(farming.entries().begin(), farming.entries().end());
    REQUIRE(snapshot.size() == 1);

    FarmingService restored(&content);
    REQUIRE(restored.till(TileCoord{2, 1}, map).isOk());
    restored.restore(snapshot);

    CHECK(restored.events().empty());
    CHECK(restored.entries().size() == 1);
    REQUIRE(restored.tileAt(cell) != nullptr);
    CHECK(restored.tileAt(cell)->tilled);
    CHECK(restored.tileAt(cell)->watered == farming.tileAt(cell)->watered);
    CHECK(restored.tileAt(cell)->daysSinceWatered == farming.tileAt(cell)->daysSinceWatered);
    CHECK(restored.tileAt(cell)->greenhouse == farming.tileAt(cell)->greenhouse);
    REQUIRE(restored.cropAt(cell) != nullptr);
    CHECK(restored.cropAt(cell)->crop == farming.cropAt(cell)->crop);
    CHECK(restored.cropAt(cell)->seed == ContentId::fromName("item.seed"));
    CHECK(restored.cropAt(cell)->stage == farming.cropAt(cell)->stage);
    CHECK(restored.cropAt(cell)->growthDays == Approx(farming.cropAt(cell)->growthDays));
    CHECK(restored.cropAt(TileCoord{2, 1}) == nullptr);

    restored.restore({});
    CHECK(restored.entries().empty());
    CHECK(restored.tileAt(cell) == nullptr);
}

TEST_CASE("Farming tool handler rejects a missing seed", "[farming][tool]") {
    const Tilemap map = makeMap();
    const ContentDatabase content = makeContent();
    FarmingService farming(&content);
    Inventory inventory(1, 99, &content);
    FarmingToolHandler handler(farming, inventory);
    fa::scene::World world;

    fa::interaction::InteractionRequest request;
    request.tool = fa::interaction::ToolKind::Seed;
    request.target.cell = TileCoord{1, 1};
    request.target.tile = map.tileAt(1, 1, 0);
    request.target.valid = true;
    request.target.reachable = true;
    const fa::interaction::ToolUseContext context{world, map, nullptr, request};

    CHECK(handler.use(context).error().code == fa::farming::farming_error::kNoSeedSelected);
    handler.setSelectedSeed(ContentId::fromName("item.seed"));
    CHECK(handler.use(context).error().code == fa::farming::farming_error::kNoSeedSelected);
    REQUIRE(farming.till(TileCoord{1, 1}, map).isOk());
    CHECK(handler.use(context).error().code == fa::farming::farming_error::kNoSeedSelected);
}
