/// \file
/// \brief Tests for M17: target selection, entity priority, facing targets, line of
/// sight, tool dispatch, placement validation and occupancy. Everything is headless;
/// the service produces plain targets/events and never touches a renderer.
/// See docs/build/m17-interaction-grid.md for the problems found while writing this.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

#include <fa/core/Math.hpp>
#include <fa/core/Result.hpp>
#include <fa/interaction/Interactable.hpp>
#include <fa/interaction/InteractionError.hpp>
#include <fa/interaction/InteractionService.hpp>
#include <fa/interaction/InteractionTypes.hpp>
#include <fa/interaction/PlacementGrid.hpp>
#include <fa/interaction/ToolHandler.hpp>
#include <fa/physics/Collider.hpp>
#include <fa/scene/Components.hpp>
#include <fa/scene/World.hpp>
#include <fa/tilemap/TileId.hpp>
#include <fa/tilemap/Tilemap.hpp>

namespace {

namespace err = fa::interaction::interaction_error;

using Catch::Approx;
using fa::core::Vec2;
using fa::interaction::Facing;
using fa::interaction::GridTarget;
using fa::interaction::Interactable;
using fa::interaction::InteractionEventKind;
using fa::interaction::InteractionOptions;
using fa::interaction::InteractionRequest;
using fa::interaction::InteractionService;
using fa::interaction::PlacementGrid;
using fa::interaction::PlacementRequest;
using fa::interaction::ToolKind;
using fa::scene::EntityId;
using fa::scene::Transform;
using fa::scene::World;
using fa::tilemap::TileCoord;
using fa::tilemap::TileDef;
using fa::tilemap::TileId;
using fa::tilemap::TileLayer;
using fa::tilemap::Tilemap;

constexpr int kTile = 16;

Tilemap makeMap(int columns = 10, int rows = 10) {
    Tilemap map(columns, rows, kTile);
    TileDef grass;
    grass.id = TileId{1};
    grass.name = "grass";
    static_cast<void>(map.addTileDef(grass));

    TileDef wall;
    wall.id = TileId{2};
    wall.name = "wall";
    wall.walkable = false;
    wall.solid = true;
    static_cast<void>(map.addTileDef(wall));

    TileDef water;
    water.id = TileId{3};
    water.name = "water";
    water.walkable = false;
    water.water = true;
    static_cast<void>(map.addTileDef(water));

    TileLayer ground;
    ground.name = "ground";
    ground.renderLayer = fa::render::RenderLayer::Ground;
    ground.tiles.assign(map.cellCount(), TileId{1});
    static_cast<void>(map.addLayer(std::move(ground)));
    return map;
}

void setTile(Tilemap& map, int column, int row, TileId tile) {
    static_cast<void>(map.setTile(column, row, 0, tile));
}

EntityId makeActor(World& world, Vec2 position, Vec2 size = Vec2{8.0f, 8.0f}) {
    const EntityId id = world.create();
    Transform transform;
    transform.position = position;
    static_cast<void>(world.add<Transform>(id, transform));

    fa::physics::Collider collider;
    collider.size = size;
    collider.layer = fa::physics::CollisionLayer::Player;
    collider.mask = fa::physics::maskOf(fa::physics::CollisionLayer::World);
    static_cast<void>(world.add<fa::physics::Collider>(id, collider));
    return id;
}

EntityId makeInteractable(World& world, Vec2 position, std::uint32_t priority = 0, std::uint32_t payload = 0) {
    const EntityId id = world.create();
    Transform transform;
    transform.position = position;
    static_cast<void>(world.add<Transform>(id, transform));

    Interactable interactable;
    interactable.area = fa::core::makeRect(0.0f, 0.0f, 16.0f, 16.0f);
    interactable.priority = priority;
    interactable.payload = payload;
    static_cast<void>(world.add<Interactable>(id, interactable));
    return id;
}

EntityId makeOwner(World& world) {
    return world.create();
}

class RecordingHandler final : public fa::interaction::IToolHandler {
public:
    explicit RecordingHandler(ToolKind tool) : m_tool(tool) {}

    [[nodiscard]] bool handles(ToolKind tool) const noexcept override { return tool == m_tool; }

    [[nodiscard]] fa::core::Status use(const fa::interaction::ToolUseContext& context) override {
        ++uses;
        lastPayload = context.request.payload;
        lastCell = context.request.target.cell;
        lastEntity = context.request.target.entity;
        return result;
    }

    fa::core::Status result = fa::core::okStatus();
    std::size_t uses = 0;
    std::uint32_t lastPayload = 0;
    TileCoord lastCell{};
    EntityId lastEntity{};

private:
    ToolKind m_tool;
};

}  // namespace

TEST_CASE("Interaction names are stable", "[interaction]") {
    REQUIRE(fa::interaction::toString(Facing::North) == "north");
    REQUIRE(fa::interaction::toString(Facing::East) == "east");
    REQUIRE(fa::interaction::toString(Facing::South) == "south");
    REQUIRE(fa::interaction::toString(Facing::West) == "west");
    REQUIRE(fa::interaction::toString(ToolKind::None) == "none");
    REQUIRE(fa::interaction::toString(ToolKind::WateringCan) == "watering_can");
    REQUIRE(fa::interaction::toString(ToolKind::Pickaxe) == "pickaxe");
    REQUIRE(fa::interaction::toString(InteractionEventKind::InteractRequested) == "interact_requested");
    REQUIRE(fa::interaction::toString(InteractionEventKind::PlacementDenied) == "placement_denied");
    REQUIRE(fa::interaction::toString(InteractionEventKind::RemovalRequested) == "removal_requested");
}

TEST_CASE("Placement grid sizes and footprints", "[interaction][placement]") {
    PlacementGrid grid;
    REQUIRE(grid.columns() == 0);
    REQUIRE(grid.rows() == 0);
    REQUIRE_FALSE(grid.contains(0, 0));

    REQUIRE(grid.resize(0, 4).error().code == err::kInvalidGrid);
    REQUIRE(grid.resize(4, 0).error().code == err::kInvalidGrid);
    REQUIRE(grid.resize(PlacementGrid::kMaxDimension + 1, 4).error().code == err::kInvalidGrid);
    REQUIRE(grid.resize(3, 2).isOk());
    REQUIRE(grid.columns() == 3);
    REQUIRE(grid.rows() == 2);
    REQUIRE(grid.contains(2, 1));
    REQUIRE_FALSE(grid.contains(3, 1));
    REQUIRE_FALSE(grid.contains(-1, 0));
    REQUIRE(grid.occupiedCells() == 0);

    REQUIRE(PlacementGrid::cellsAlong(16.0f, kTile) == 1);
    REQUIRE(PlacementGrid::cellsAlong(16.1f, kTile) == 2);
    REQUIRE(PlacementGrid::cellsAlong(24.0f, kTile) == 2);
    REQUIRE(PlacementGrid::cellsAlong(32.0f, kTile) == 2);
    REQUIRE(PlacementGrid::cellsAlong(0.0f, kTile) == 0);
    REQUIRE(PlacementGrid::cellsAlong(-1.0f, kTile) == 0);

    const Tilemap map = makeMap();
    const fa::core::Rect single = PlacementGrid::footprintRect(map, TileCoord{1, 2}, Vec2{16.0f, 16.0f});
    REQUIRE(single.x == Approx(16.0f));
    REQUIRE(single.y == Approx(32.0f));
    REQUIRE(single.w == Approx(16.0f));
    REQUIRE(single.h == Approx(16.0f));

    const fa::core::Rect wide = PlacementGrid::footprintRect(map, TileCoord{1, 2}, Vec2{24.0f, 16.0f});
    REQUIRE(wide.w == Approx(32.0f));
    REQUIRE(wide.h == Approx(16.0f));
    REQUIRE(fa::core::isEmpty(PlacementGrid::footprintRect(map, TileCoord{1, 2}, Vec2{0.0f, 16.0f})));
}

TEST_CASE("Placement grid marks and removes footprints", "[interaction][placement]") {
    Tilemap map = makeMap();
    PlacementGrid grid(10, 10);
    World world;
    const EntityId owner = makeOwner(world);

    REQUIRE(grid.canPlace(map, TileCoord{1, 1}, Vec2{16.0f, 16.0f}));
    REQUIRE_FALSE(grid.canPlace(map, TileCoord{-1, 1}, Vec2{16.0f, 16.0f}));
    REQUIRE_FALSE(grid.canPlace(map, TileCoord{9, 9}, Vec2{32.0f, 16.0f}));
    REQUIRE_FALSE(grid.canPlace(map, TileCoord{1, 1}, Vec2{0.0f, 16.0f}));

    setTile(map, 2, 1, TileId{2});
    REQUIRE_FALSE(grid.canPlace(map, TileCoord{2, 1}, Vec2{16.0f, 16.0f}));
    setTile(map, 3, 1, TileId{3});
    REQUIRE_FALSE(grid.canPlace(map, TileCoord{3, 1}, Vec2{16.0f, 16.0f}));

    REQUIRE_FALSE(grid.place(map, TileCoord{1, 1}, Vec2{16.0f, 16.0f}, EntityId{}));
    REQUIRE(grid.place(map, TileCoord{1, 1}, Vec2{16.0f, 16.0f}, owner, 7));
    REQUIRE(grid.occupiedCells() == 1);
    REQUIRE(grid.isOccupied(1, 1));
    REQUIRE(grid.ownerAt(1, 1) == owner);
    REQUIRE(grid.payloadAt(1, 1) == 7);
    REQUIRE_FALSE(grid.canPlace(map, TileCoord{1, 1}, Vec2{16.0f, 16.0f}));
    REQUIRE_FALSE(grid.place(map, TileCoord{1, 1}, Vec2{16.0f, 16.0f}, owner, 8));

    REQUIRE(grid.place(map, TileCoord{4, 4}, Vec2{24.0f, 16.0f}, owner, 9));
    REQUIRE(grid.occupiedCells() == 3);
    REQUIRE(grid.isOccupied(4, 4));
    REQUIRE(grid.isOccupied(5, 4));
    REQUIRE_FALSE(grid.isOccupied(6, 4));
    REQUIRE_FALSE(grid.place(map, TileCoord{5, 4}, Vec2{16.0f, 16.0f}, owner));

    REQUIRE(grid.remove(owner) == 3);
    REQUIRE(grid.occupiedCells() == 0);
    REQUIRE_FALSE(grid.isOccupied(1, 1));
    REQUIRE(grid.remove(owner) == 0);

    REQUIRE(grid.place(map, TileCoord{1, 1}, Vec2{16.0f, 16.0f}, owner));
    grid.clear();
    REQUIRE(grid.occupiedCells() == 0);
}

TEST_CASE("Target picking prefers the highest-priority interactable", "[interaction][target]") {
    const Tilemap map = makeMap();
    World world;
    const EntityId actor = makeActor(world, Vec2{16.0f, 16.0f});
    const EntityId low = makeInteractable(world, Vec2{32.0f, 16.0f}, 1, 7);
    const EntityId high = makeInteractable(world, Vec2{32.0f, 16.0f}, 5, 9);
    const EntityId far = makeInteractable(world, Vec2{200.0f, 200.0f}, 99, 11);
    static_cast<void>(low);
    static_cast<void>(far);

    InteractionService service(map);
    const GridTarget target = service.pickTarget(world, actor, Vec2{40.0f, 24.0f});
    REQUIRE(target.valid);
    REQUIRE(target.entity == high);
    REQUIRE(target.payload == 9);
    REQUIRE(target.cell == TileCoord{2, 1});
    REQUIRE(target.reachable);
    REQUIRE(target.distance == Approx(20.396f).margin(0.01f));
}

TEST_CASE("Target picking falls back to tiles and reports unreachable points", "[interaction][target]") {
    const Tilemap map = makeMap();
    World world;
    const EntityId actor = makeActor(world, Vec2{16.0f, 16.0f});
    InteractionService service(map);

    const GridTarget tile = service.pickTarget(world, actor, Vec2{24.0f, 24.0f});
    REQUIRE(tile.valid);
    REQUIRE_FALSE(tile.entity.isValid());
    REQUIRE(tile.tile == TileId{1});
    REQUIRE(tile.cell == TileCoord{1, 1});
    REQUIRE(tile.reachable);

    const GridTarget far = service.pickTarget(world, actor, Vec2{152.0f, 152.0f});
    REQUIRE(far.valid);
    REQUIRE(far.cell == TileCoord{9, 9});
    REQUIRE_FALSE(far.reachable);

    REQUIRE_FALSE(service.pickTarget(world, actor, Vec2{-4.0f, -4.0f}).valid);
    REQUIRE_FALSE(service.pickTarget(world, EntityId{}, Vec2{24.0f, 24.0f}).valid);
}

TEST_CASE("Facing targets use the adjacent cell", "[interaction][target]") {
    const Tilemap map = makeMap();
    World world;
    const EntityId actor = makeActor(world, Vec2{16.0f, 16.0f});
    const EntityId crate = makeInteractable(world, Vec2{32.0f, 16.0f}, 0, 3);

    InteractionService service(map);
    const GridTarget east = service.pickTargetInFront(world, actor, Facing::East);
    REQUIRE(east.valid);
    REQUIRE(east.cell == TileCoord{2, 1});
    REQUIRE(east.entity == crate);
    REQUIRE(east.reachable);

    const GridTarget west = service.pickTargetInFront(world, actor, Facing::West);
    REQUIRE(west.valid);
    REQUIRE(west.cell == TileCoord{0, 1});
    REQUIRE_FALSE(west.entity.isValid());

    REQUIRE_FALSE(service.pickTargetInFront(world, actor, Facing::Count).valid);
}

TEST_CASE("Line of sight blocks walls but allows the target cell", "[interaction][sight]") {
    Tilemap map = makeMap();
    setTile(map, 3, 1, TileId{2});

    REQUIRE(InteractionService::hasLineOfSight(map, TileCoord{1, 1}, TileCoord{2, 1}));
    REQUIRE_FALSE(InteractionService::hasLineOfSight(map, TileCoord{1, 1}, TileCoord{4, 1}));
    REQUIRE(InteractionService::hasLineOfSight(map, TileCoord{1, 1}, TileCoord{3, 1}));
    REQUIRE(InteractionService::hasLineOfSight(map, TileCoord{1, 1}, TileCoord{1, 1}));

    setTile(map, 2, 1, TileId{2});
    setTile(map, 1, 2, TileId{2});
    REQUIRE_FALSE(InteractionService::hasLineOfSight(map, TileCoord{1, 1}, TileCoord{2, 2}));
    setTile(map, 2, 1, TileId{1});
    REQUIRE_FALSE(InteractionService::hasLineOfSight(map, TileCoord{1, 1}, TileCoord{2, 2}));
    setTile(map, 1, 2, TileId{1});
    REQUIRE(InteractionService::hasLineOfSight(map, TileCoord{1, 1}, TileCoord{2, 2}));
}

TEST_CASE("Range and line of sight gate canInteract", "[interaction][target]") {
    Tilemap map = makeMap();
    setTile(map, 3, 1, TileId{2});
    World world;
    const EntityId actor = makeActor(world, Vec2{16.0f, 16.0f});

    InteractionService service(map);
    InteractionOptions options;
    options.maxDistance = 64.0f;
    service.setOptions(options);

    const GridTarget blocked = service.pickTarget(world, actor, Vec2{72.0f, 24.0f});
    REQUIRE(blocked.valid);
    REQUIRE(blocked.cell == TileCoord{4, 1});
    REQUIRE_FALSE(blocked.reachable);
    REQUIRE_FALSE(service.canInteract(world, actor, blocked, ToolKind::Hoe));

    options.requireLineOfSight = false;
    service.setOptions(options);
    const GridTarget open = service.pickTarget(world, actor, Vec2{72.0f, 24.0f});
    REQUIRE(open.reachable);
    REQUIRE(service.canInteract(world, actor, open, ToolKind::Hoe));
}

TEST_CASE("canInteract applies tile and entity rules", "[interaction][target]") {
    Tilemap map = makeMap();
    setTile(map, 2, 1, TileId{2});
    World world;
    const EntityId actor = makeActor(world, Vec2{16.0f, 16.0f});
    InteractionService service(map);

    const GridTarget wall = service.pickTarget(world, actor, Vec2{40.0f, 24.0f});
    REQUIRE(wall.valid);
    REQUIRE(wall.tile == TileId{2});
    REQUIRE_FALSE(service.canInteract(world, actor, wall, ToolKind::Hoe));
    REQUIRE_FALSE(service.canInteract(world, actor, wall, ToolKind::None));
    REQUIRE(service.canInteract(world, actor, wall, ToolKind::Pickaxe));

    setTile(map, 1, 1, fa::tilemap::kEmptyTile);
    const GridTarget empty = service.pickTarget(world, actor, Vec2{24.0f, 24.0f});
    REQUIRE(empty.valid);
    REQUIRE_FALSE(empty.tile.isValid());
    REQUIRE_FALSE(service.canInteract(world, actor, empty, ToolKind::Hoe));

    const EntityId crate = makeInteractable(world, Vec2{32.0f, 16.0f});
    const GridTarget entity = service.pickTarget(world, actor, Vec2{40.0f, 24.0f});
    REQUIRE(entity.entity == crate);
    REQUIRE(service.canInteract(world, actor, entity, ToolKind::Hoe));
    world.tryGet<Interactable>(crate)->enabled = false;
    REQUIRE_FALSE(service.canInteract(world, actor, entity, ToolKind::Hoe));
}

TEST_CASE("Interact dispatches to the first matching handler", "[interaction][tool]") {
    const Tilemap map = makeMap();
    World world;
    const EntityId actor = makeActor(world, Vec2{16.0f, 16.0f});
    const EntityId crate = makeInteractable(world, Vec2{32.0f, 16.0f}, 0, 5);

    InteractionService service(map);
    RecordingHandler hoe(ToolKind::Hoe);
    RecordingHandler watering(ToolKind::WateringCan);
    service.addHandler(hoe);
    service.addHandler(watering);
    service.addHandler(hoe);
    REQUIRE(service.handlerCount() == 2);

    InteractionRequest request;
    request.actor = actor;
    request.target = service.pickTarget(world, actor, Vec2{40.0f, 24.0f});
    request.tool = ToolKind::Hoe;
    request.payload = 77;

    REQUIRE(service.interact(world, request).isOk());
    REQUIRE(hoe.uses == 1);
    REQUIRE(watering.uses == 0);
    REQUIRE(hoe.lastPayload == 77);
    REQUIRE(hoe.lastCell == TileCoord{2, 1});
    REQUIRE(hoe.lastEntity == crate);
    REQUIRE(service.events().size() == 2);
    REQUIRE(service.events()[0].kind == InteractionEventKind::InteractRequested);
    REQUIRE(service.events()[0].errorCode == 0);
    REQUIRE(service.events()[1].kind == InteractionEventKind::ToolUsed);
    REQUIRE(service.events()[1].errorCode == 0);

    service.clearEvents();
    InteractionRequest unsupported = request;
    unsupported.tool = ToolKind::Seed;
    const fa::core::Status missing = service.interact(world, unsupported);
    REQUIRE(missing.isError());
    REQUIRE(missing.error().code == err::kNoHandler);
    REQUIRE(hoe.uses == 1);
    REQUIRE(service.events().size() == 2);
    REQUIRE(service.events()[0].kind == InteractionEventKind::InteractRequested);
    REQUIRE(service.events()[1].kind == InteractionEventKind::ToolUsed);
    REQUIRE(service.events()[1].errorCode == err::kNoHandler);

    service.clearHandlers();
    REQUIRE(service.handlerCount() == 0);
    REQUIRE(service.interact(world, request).error().code == err::kNoHandler);
}

TEST_CASE("Interact rejects invalid, out-of-range and blocked targets", "[interaction][tool]") {
    Tilemap map = makeMap();
    setTile(map, 3, 1, TileId{2});
    World world;
    const EntityId actor = makeActor(world, Vec2{16.0f, 16.0f});
    InteractionService service(map);
    RecordingHandler hoe(ToolKind::Hoe);
    service.addHandler(hoe);

    InteractionRequest request;
    request.actor = actor;
    request.target = service.pickTarget(world, actor, Vec2{24.0f, 24.0f});
    request.tool = ToolKind::Hoe;
    REQUIRE(service.interact(world, request).isOk());

    request.actor = EntityId{};
    service.clearEvents();
    REQUIRE(service.interact(world, request).error().code == err::kInvalidActor);
    REQUIRE(service.events().size() == 1);
    REQUIRE(service.events()[0].errorCode == err::kInvalidActor);

    request.actor = actor;
    request.target = GridTarget{};
    service.clearEvents();
    REQUIRE(service.interact(world, request).error().code == err::kInvalidTarget);

    InteractionOptions options;
    options.maxDistance = 64.0f;
    service.setOptions(options);
    request.target = service.pickTarget(world, actor, Vec2{136.0f, 136.0f});
    service.clearEvents();
    REQUIRE(service.interact(world, request).error().code == err::kOutOfRange);

    request.target = service.pickTarget(world, actor, Vec2{72.0f, 24.0f});
    REQUIRE_FALSE(request.target.reachable);
    service.clearEvents();
    REQUIRE(service.interact(world, request).error().code == err::kBlocked);
}

TEST_CASE("Placement validation checks range, walkability, size and ownership", "[interaction][placement]") {
    Tilemap map = makeMap();
    PlacementGrid grid(10, 10);
    World world;
    const EntityId actor = makeActor(world, Vec2{16.0f, 16.0f});
    const EntityId owner = makeOwner(world);
    InteractionService service(map, &grid);

    PlacementRequest request;
    request.actor = actor;
    request.owner = owner;
    request.cell = TileCoord{2, 1};
    request.size = Vec2{16.0f, 16.0f};
    request.payload = 42;

    REQUIRE(service.canPlace(world, request).isOk());
    const fa::interaction::PlacementPreview preview = service.previewPlacement(world, request);
    REQUIRE(preview.valid);
    REQUIRE(preview.errorCode == 0);
    REQUIRE(preview.bounds.x == Approx(32.0f));
    REQUIRE(preview.bounds.y == Approx(16.0f));
    REQUIRE(preview.bounds.w == Approx(16.0f));
    REQUIRE(preview.bounds.h == Approx(16.0f));

    setTile(map, 2, 1, TileId{2});
    REQUIRE(service.canPlace(world, request).error().code == err::kNotPlaceable);
    const fa::interaction::PlacementPreview rejected = service.previewPlacement(world, request);
    REQUIRE_FALSE(rejected.valid);
    REQUIRE(rejected.errorCode == err::kNotPlaceable);

    request.cell = TileCoord{8, 8};
    REQUIRE(service.canPlace(world, request).error().code == err::kOutOfRange);

    request.cell = TileCoord{2, 1};
    request.size = Vec2{0.0f, 16.0f};
    REQUIRE(service.canPlace(world, request).error().code == err::kInvalidFootprint);
    request.size = Vec2{16.0f, 16.0f};

    request.owner = EntityId{};
    REQUIRE(service.canPlace(world, request).error().code == err::kInvalidOwner);
    setTile(map, 2, 1, TileId{1});
    request.owner = makeOwner(world);
    REQUIRE(service.canPlace(world, request).isOk());
    world.destroy(request.owner);
    REQUIRE(service.canPlace(world, request).error().code == err::kInvalidOwner);

    InteractionService noGrid(map);
    request.owner = owner;
    REQUIRE(noGrid.canPlace(world, request).error().code == err::kNoPlacementGrid);
}

TEST_CASE("Placement requires the requested area kind", "[interaction][placement]") {
    Tilemap map = makeMap();
    map.addArea(fa::tilemap::MapArea{1, fa::tilemap::AreaKind::Greenhouse, fa::tilemap::TileBounds{2, 1, 2, 2}});
    PlacementGrid grid(10, 10);
    World world;
    const EntityId actor = makeActor(world, Vec2{16.0f, 16.0f});
    const EntityId owner = makeOwner(world);
    InteractionService service(map, &grid);
    InteractionOptions options;
    options.maxDistance = 100.0f;
    service.setOptions(options);

    PlacementRequest request;
    request.actor = actor;
    request.owner = owner;
    request.cell = TileCoord{2, 1};
    request.size = Vec2{16.0f, 16.0f};
    request.requiredArea = fa::tilemap::AreaKind::Greenhouse;
    REQUIRE(service.canPlace(world, request).isOk());

    request.cell = TileCoord{4, 2};
    REQUIRE(service.canPlace(world, request).error().code == err::kInvalidArea);
}

TEST_CASE("Placement and removal update occupancy and events", "[interaction][placement]") {
    Tilemap map = makeMap();
    PlacementGrid grid(10, 10);
    World world;
    const EntityId actor = makeActor(world, Vec2{16.0f, 16.0f});
    const EntityId owner = makeOwner(world);
    InteractionService service(map, &grid);

    PlacementRequest request;
    request.actor = actor;
    request.owner = owner;
    request.cell = TileCoord{2, 1};
    request.size = Vec2{16.0f, 16.0f};
    request.payload = 42;

    REQUIRE(service.requestPlacement(world, request).isOk());
    REQUIRE(grid.occupiedCells() == 1);
    REQUIRE(grid.ownerAt(2, 1) == owner);
    REQUIRE(grid.payloadAt(2, 1) == 42);
    REQUIRE(service.events().size() == 1);
    REQUIRE(service.events()[0].kind == InteractionEventKind::PlacementRequested);
    REQUIRE(service.events()[0].payload == 42);

    const fa::interaction::PlacementPreview occupied = service.previewPlacement(world, request);
    REQUIRE_FALSE(occupied.valid);
    REQUIRE(occupied.errorCode == err::kOccupied);

    service.clearEvents();
    REQUIRE(service.requestPlacement(world, request).error().code == err::kOccupied);
    REQUIRE(service.events().size() == 1);
    REQUIRE(service.events()[0].kind == InteractionEventKind::PlacementDenied);
    REQUIRE(service.events()[0].errorCode == err::kOccupied);

    service.clearEvents();
    REQUIRE(service.requestRemoval(world, actor, TileCoord{2, 1}).isOk());
    REQUIRE(grid.occupiedCells() == 0);
    REQUIRE(service.events().size() == 1);
    REQUIRE(service.events()[0].kind == InteractionEventKind::RemovalRequested);
    REQUIRE(service.events()[0].entity == owner);
    REQUIRE(service.events()[0].payload == 42);

    service.clearEvents();
    REQUIRE(service.requestRemoval(world, actor, TileCoord{2, 1}).error().code == err::kNotOccupied);
    REQUIRE(service.events().size() == 1);
    REQUIRE(service.events()[0].kind == InteractionEventKind::PlacementDenied);

    REQUIRE(service.requestRemoval(world, actor, TileCoord{1, 1}).error().code == err::kNotOccupied);
}

TEST_CASE("Actor centre follows the collider when present", "[interaction][target]") {
    const Tilemap map = makeMap();
    World world;
    InteractionService service(map);

    const EntityId actor = makeActor(world, Vec2{10.0f, 10.0f}, Vec2{8.0f, 6.0f});
    fa::physics::Collider* collider = world.tryGet<fa::physics::Collider>(actor);
    REQUIRE(collider != nullptr);
    collider->offset = Vec2{2.0f, 4.0f};
    REQUIRE(service.actorCenter(world, actor) == Vec2{16.0f, 17.0f});

    const EntityId bare = world.create();
    Transform transform;
    transform.position = Vec2{10.0f, 10.0f};
    static_cast<void>(world.add<Transform>(bare, transform));
    InteractionOptions options;
    options.actorFallbackSize = Vec2{10.0f, 10.0f};
    service.setOptions(options);
    REQUIRE(service.actorCenter(world, bare) == Vec2{15.0f, 15.0f});
    REQUIRE(service.actorCenter(world, EntityId{}) == Vec2{0.0f, 0.0f});
}
