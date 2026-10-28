/// \file
/// \brief Tests for M13: collision layers and masks, the collider geometry, the
/// broadphase, the tile grid, the axis-separated solver, overlap and ray queries,
/// trigger enter/leave events and the fixed-step wrapper. Everything here is
/// headless - physics produces plain rectangles and ids, so no GL context is needed.
/// See docs/build/m13-physics-collision.md for the problems found while writing this.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include <fa/core/Math.hpp>
#include <fa/physics/Collider.hpp>
#include <fa/physics/CollisionLayer.hpp>
#include <fa/physics/PhysicsError.hpp>
#include <fa/physics/PhysicsSystem.hpp>
#include <fa/physics/PhysicsWorld.hpp>
#include <fa/physics/SpatialHash.hpp>
#include <fa/physics/TileCollision.hpp>
#include <fa/scene/Components.hpp>
#include <fa/scene/Scene.hpp>
#include <fa/scene/World.hpp>
#include <fa/time/FrameContext.hpp>

namespace {

using Catch::Approx;
using fa::core::Rect;
using fa::core::Vec2;
using fa::physics::Collider;
using fa::physics::CollisionLayer;
using fa::physics::DebugShape;
using fa::physics::PhysicsStats;
using fa::physics::PhysicsSystem;
using fa::physics::PhysicsWorld;
using fa::physics::RayHit;
using fa::physics::SpatialHash;
using fa::physics::TileGrid;
using fa::physics::TriggerEvent;
using fa::physics::TriggerPhase;
using fa::physics::Velocity;
using fa::scene::BasicScene;
using fa::scene::EntityId;
using fa::scene::Transform;
using fa::scene::World;

constexpr float kTile = 16.0f;

/// \brief A plain body that collides with world geometry.
Collider makeBox(Vec2 size,
                 CollisionLayer layer = CollisionLayer::Player,
                 fa::physics::CollisionMask mask = fa::physics::maskOf(CollisionLayer::World)) {
    Collider collider;
    collider.size = size;
    collider.layer = layer;
    collider.mask = mask;
    return collider;
}

/// \brief Adds a transform (plus an optional collider and velocity) and returns the id.
EntityId makeBody(World& world, Vec2 position, const Collider* collider = nullptr, const Velocity* velocity = nullptr) {
    const EntityId id = world.create();
    Transform transform;
    transform.position = position;
    static_cast<void>(world.add<Transform>(id, transform));
    if (collider != nullptr) {
        static_cast<void>(world.add<Collider>(id, *collider));
    }
    if (velocity != nullptr) {
        static_cast<void>(world.add<Velocity>(id, *velocity));
    }
    return id;
}

Vec2 positionOf(const World& world, EntityId id) {
    const Transform* transform = world.tryGet<Transform>(id);
    return transform != nullptr ? transform->position : Vec2{-1.0f, -1.0f};
}

}  // namespace

TEST_CASE("Collision layers combine into masks and match both ways", "[physics][layer]") {
    using fa::physics::maskOf;
    using fa::physics::matches;

    REQUIRE(maskOf(CollisionLayer::None) == 0u);
    REQUIRE(maskOf(CollisionLayer::World) == 1u << 0);
    REQUIRE(maskOf(CollisionLayer::Player) == 1u << 1);
    REQUIRE(maskOf(CollisionLayer::All) == 0xFFFFFFFFu);

    const auto both = CollisionLayer::World | CollisionLayer::Enemy;
    REQUIRE(both == (maskOf(CollisionLayer::World) | maskOf(CollisionLayer::Enemy)));
    REQUIRE(matches(maskOf(CollisionLayer::Enemy), both));
    REQUIRE_FALSE(matches(maskOf(CollisionLayer::Player), both));

    const auto widened = both | CollisionLayer::Player;
    REQUIRE(matches(maskOf(CollisionLayer::Player), widened));

    // Every named layer must be inside All, which is what makes "collide with
    // everything" a usable default.
    REQUIRE(matches(maskOf(CollisionLayer::World), maskOf(CollisionLayer::All)));
    REQUIRE(matches(maskOf(CollisionLayer::Trigger), maskOf(CollisionLayer::All)));
}

TEST_CASE("Collision layer names are stable", "[physics][layer]") {
    using fa::physics::toString;

    REQUIRE(toString(CollisionLayer::None) == "None");
    REQUIRE(toString(CollisionLayer::World) == "World");
    REQUIRE(toString(CollisionLayer::Player) == "Player");
    REQUIRE(toString(CollisionLayer::Enemy) == "Enemy");
    REQUIRE(toString(CollisionLayer::PlayerAttack) == "PlayerAttack");
    REQUIRE(toString(CollisionLayer::EnemyAttack) == "EnemyAttack");
    REQUIRE(toString(CollisionLayer::Interactable) == "Interactable");
    REQUIRE(toString(CollisionLayer::Trigger) == "Trigger");
    REQUIRE(toString(CollisionLayer::All) == "All");
    REQUIRE(toString(static_cast<CollisionLayer>(1u << 9)) == "Unknown");
}

TEST_CASE("Collider boxes are offset from the transform position", "[physics][collider]") {
    Transform transform;
    transform.position = Vec2{100.0f, 200.0f};

    Collider collider;
    collider.offset = Vec2{3.0f, 18.0f};
    collider.size = Vec2{10.0f, 6.0f};

    const Rect rect = fa::physics::colliderRect(transform, collider);
    REQUIRE(rect.x == Approx(103.0f));
    REQUIRE(rect.y == Approx(218.0f));
    REQUIRE(rect.w == Approx(10.0f));
    REQUIRE(rect.h == Approx(6.0f));

    // Rotation and scale are presentation only: they must not move the box.
    transform.rotation = 1.0f;
    transform.scale = Vec2{4.0f, 4.0f};
    const Rect scaled = fa::physics::colliderRect(transform, collider);
    REQUIRE(scaled.x == Approx(103.0f));
    REQUIRE(scaled.w == Approx(10.0f));
}

TEST_CASE("The feet collider matches the ADR-0003 metrics", "[physics][collider]") {
    const Collider& feet = fa::physics::kPlayerFeetCollider;
    REQUIRE(feet.offset.x == Approx(3.0f));
    REQUIRE(feet.offset.y == Approx(18.0f));
    REQUIRE(feet.size.x == Approx(10.0f));
    REQUIRE(feet.size.y == Approx(6.0f));
    REQUIRE(feet.layer == CollisionLayer::Player);
    REQUIRE_FALSE(feet.isTrigger);

    // A 16x24 sprite drawn from its top-left must have its feet box land exactly on
    // the sprite's bottom edge: 18 + 6 == 24.
    REQUIRE(feet.offset.y + feet.size.y == Approx(24.0f));
    // The box keeps 3 px of margin on each side of a 16 px wide frame.
    REQUIRE((feet.offset.x * 2.0f + feet.size.x) == Approx(16.0f));
}

TEST_CASE("Triggers never block movement", "[physics][collider]") {
    Collider solid;
    Collider trigger;
    trigger.isTrigger = true;
    REQUIRE(fa::physics::blocksMovement(solid));
    REQUIRE_FALSE(fa::physics::blocksMovement(trigger));
}

TEST_CASE("Two colliders interact only when both masks agree", "[physics][collider]") {
    using fa::physics::layersInteract;
    using fa::physics::maskOf;

    Collider player = makeBox(Vec2{10.0f, 6.0f}, CollisionLayer::Player, maskOf(CollisionLayer::World));
    Collider enemy = makeBox(Vec2{10.0f, 6.0f}, CollisionLayer::Enemy, maskOf(CollisionLayer::World));
    REQUIRE_FALSE(layersInteract(player, enemy));

    enemy.mask = maskOf(CollisionLayer::World) | CollisionLayer::Player;
    REQUIRE_FALSE(layersInteract(player, enemy));

    player.mask = maskOf(CollisionLayer::World) | CollisionLayer::Enemy;
    REQUIRE(layersInteract(player, enemy));
}

TEST_CASE("Spatial hash maps boxes onto cells", "[physics][hash]") {
    SpatialHash hash(64.0f);
    REQUIRE(hash.cellSize() == Approx(64.0f));

    const EntityId first{1, 1};
    hash.insert(first, Rect{0.0f, 0.0f, 64.0f, 64.0f});
    // A box ending exactly on the grid line stays in one cell.
    REQUIRE(hash.cellCount() == 1u);
    REQUIRE(hash.entryCount() == 1u);

    const EntityId second{2, 1};
    hash.insert(second, Rect{32.0f, 32.0f, 64.0f, 64.0f});
    // Second box spans cells (0,0), (1,0), (0,1), (1,1).
    REQUIRE(hash.entryCount() == 5u);
    REQUIRE(hash.maxCellOccupancy() == 2u);

    const auto hits = hash.queryUnique(Rect{0.0f, 0.0f, 8.0f, 8.0f});
    // Both boxes occupy cell (0,0): the second one starts at 32, so it reaches into the
    // first cell as well. A broadphase is allowed to over-fetch; the narrow phase filters.
    REQUIRE(hits.size() == 2u);
    REQUIRE(hits[0] == first);
    REQUIRE(hits[1] == second);
}

TEST_CASE("Spatial hash query is sorted and duplicate free", "[physics][hash]") {
    SpatialHash hash(16.0f);
    // Three ids that all share the same cell, inserted out of order.
    hash.insert(EntityId{7, 1}, Rect{0.0f, 0.0f, 4.0f, 4.0f});
    hash.insert(EntityId{2, 1}, Rect{2.0f, 2.0f, 4.0f, 4.0f});
    hash.insert(EntityId{5, 1}, Rect{4.0f, 4.0f, 4.0f, 4.0f});

    const auto hits = hash.queryUnique(Rect{0.0f, 0.0f, 8.0f, 8.0f});
    REQUIRE(hits.size() == 3u);
    REQUIRE(hits[0].index == 2u);
    REQUIRE(hits[1].index == 5u);
    REQUIRE(hits[2].index == 7u);

    // Repeated queries must not accumulate the counter twice per id.
    const auto again = hash.queryUnique(Rect{0.0f, 0.0f, 8.0f, 8.0f});
    REQUIRE(again.size() == 3u);
}

TEST_CASE("Spatial hash handles empty areas and rebuilds", "[physics][hash]") {
    SpatialHash hash(32.0f);
    REQUIRE(hash.queryUnique(Rect{100.0f, 100.0f, 4.0f, 4.0f}).empty());

    hash.insert(EntityId{1, 1}, Rect{0.0f, 0.0f, 4.0f, 4.0f});
    const std::size_t rebuildsBefore = hash.rebuilds();
    hash.clear();
    REQUIRE(hash.entryCount() == 0u);
    REQUIRE(hash.cellCount() == 0u);
    REQUIRE(hash.maxCellOccupancy() == 0u);
    REQUIRE(hash.rebuilds() == rebuildsBefore + 1u);

    // A non-positive cell size falls back to the default instead of dividing by zero.
    hash.setCellSize(0.0f);
    REQUIRE(hash.cellSize() == Approx(64.0f));
}

TEST_CASE("Tile grid knows its extent and its outside", "[physics][tiles]") {
    TileGrid grid(4, 3, kTile);
    REQUIRE(grid.columns() == 4);
    REQUIRE(grid.rows() == 3);
    REQUIRE(grid.tileSize() == Approx(kTile));
    REQUIRE(grid.solidCount() == 0u);
    REQUIRE(grid.outsideIsSolid());

    REQUIRE(grid.isSolid(-1, 0));
    REQUIRE_FALSE(grid.isSolid(0, 0));
    REQUIRE(grid.isSolid(4, 0));
    REQUIRE(grid.isSolid(0, 3));

    REQUIRE(grid.setSolid(1, 1, true));
    REQUIRE(grid.isSolid(1, 1));
    REQUIRE(grid.solidCount() == 1u);
    // Idempotent: setting the same value twice must not double count.
    REQUIRE(grid.setSolid(1, 1, true));
    REQUIRE(grid.solidCount() == 1u);
    REQUIRE(grid.setSolid(1, 1, false));
    REQUIRE(grid.solidCount() == 0u);

    REQUIRE_FALSE(grid.setSolid(-1, 0, true));
    REQUIRE_FALSE(grid.setSolid(4, 0, true));
}

TEST_CASE("Tile grid borders and clearing", "[physics][tiles]") {
    TileGrid grid(4, 3, kTile);
    grid.setBorderSolid(true);
    // 4 + 4 on the top and bottom rows, plus 2 on the middle row.
    REQUIRE(grid.solidCount() == 10u);
    REQUIRE(grid.isSolid(0, 0));
    REQUIRE(grid.isSolid(3, 2));
    REQUIRE(grid.isSolid(0, 1));
    REQUIRE_FALSE(grid.isSolid(1, 1));

    grid.clearSolid();
    REQUIRE(grid.solidCount() == 0u);

    grid.setOutsideIsSolid(false);
    REQUIRE_FALSE(grid.isSolid(-5, -5));
}

TEST_CASE("Tile grid resets its outside rule on resize", "[physics][tiles]") {
    TileGrid grid(2, 2, kTile);
    grid.setOutsideIsSolid(false);
    grid.resize(8, 8, kTile);
    REQUIRE(grid.outsideIsSolid());
    REQUIRE(grid.columns() == 8);
    REQUIRE(grid.rows() == 8);
    REQUIRE(grid.solidCount() == 0u);

    // Non-positive dimensions collapse to empty, and a bad tile size falls back.
    grid.resize(-2, 0, 0.0f);
    REQUIRE(grid.columns() == 0);
    REQUIRE(grid.rows() == 0);
    REQUIRE(grid.tileSize() == Approx(kTile));
}

TEST_CASE("Tile overlap uses the half-open rectangle rule", "[physics][tiles]") {
    using fa::physics::overlapsSolidTile;

    TileGrid grid(4, 4, kTile);
    grid.setOutsideIsSolid(false);
    grid.setSolid(1, 1, true);

    // Cell (1,1) covers [16,32) on both axes.
    REQUIRE_FALSE(overlapsSolidTile(grid, Rect{0.0f, 0.0f, 16.0f, 16.0f}));
    REQUIRE(overlapsSolidTile(grid, Rect{15.0f, 15.0f, 2.0f, 2.0f}));
    REQUIRE(overlapsSolidTile(grid, Rect{16.0f, 16.0f, 8.0f, 8.0f}));
    // A box ending exactly on the boundary does not reach into the next cell.
    REQUIRE_FALSE(overlapsSolidTile(grid, Rect{0.0f, 0.0f, 32.0f, 16.0f}));
    REQUIRE_FALSE(overlapsSolidTile(grid, Rect{0.0f, 0.0f, 0.0f, 0.0f}));
}

TEST_CASE("Sync indexes every collider and counts triggers", "[physics][world]") {
    World world;
    const Collider body = makeBox(Vec2{10.0f, 6.0f});
    Collider trigger = makeBox(Vec2{20.0f, 20.0f}, CollisionLayer::Trigger);
    trigger.isTrigger = true;

    static_cast<void>(makeBody(world, Vec2{0.0f, 0.0f}, &body));
    static_cast<void>(makeBody(world, Vec2{100.0f, 0.0f}, &body));
    static_cast<void>(makeBody(world, Vec2{50.0f, 50.0f}, &trigger));

    PhysicsWorld physics;
    physics.sync(world);

    REQUIRE(physics.stats().bodies == 3u);
    REQUIRE(physics.stats().triggerBodies == 1u);
    REQUIRE(physics.stats().cells > 0u);
}

TEST_CASE("An unobstructed move lands exactly on the target", "[physics][move]") {
    World world;
    const Collider box = makeBox(Vec2{10.0f, 6.0f});
    const EntityId id = makeBody(world, Vec2{20.0f, 20.0f}, &box);

    PhysicsWorld physics;
    const fa::physics::MoveResult result = physics.move(world, id, Vec2{12.5f, -3.0f});

    REQUIRE(result.hitX == false);
    REQUIRE(result.hitY == false);
    // The longest axis is 12.5 px, split into 4 px sweeps.
    REQUIRE(result.substeps == 4);
    REQUIRE(result.position.x == Approx(32.5f));
    REQUIRE(result.position.y == Approx(17.0f));
    REQUIRE(positionOf(world, id).x == Approx(32.5f));
}

TEST_CASE("An entity without a collider just teleports", "[physics][move]") {
    World world;
    const EntityId id = makeBody(world, Vec2{0.0f, 0.0f});
    TileGrid grid(8, 8, kTile);
    grid.setSolid(0, 0, true);

    PhysicsWorld physics;
    physics.setTiles(&grid);
    const fa::physics::MoveResult result = physics.move(world, id, Vec2{40.0f, 0.0f});
    REQUIRE(result.position.x == Approx(40.0f));
    REQUIRE_FALSE(result.hitX);
}

TEST_CASE("A tile wall stops horizontal movement flush", "[physics][move]") {
    World world;
    TileGrid grid(20, 20, kTile);
    grid.setSolid(3, 0, true);

    const Collider box = makeBox(Vec2{10.0f, 6.0f});
    const EntityId id = makeBody(world, Vec2{20.0f, 4.0f}, &box);

    PhysicsWorld physics;
    physics.setTiles(&grid);
    const fa::physics::MoveResult result = physics.move(world, id, Vec2{100.0f, 0.0f});

    // The wall starts at x = 48, the box is 10 wide, so it rests at 38.
    REQUIRE(result.hitX);
    REQUIRE_FALSE(result.hitY);
    REQUIRE(result.position.x == Approx(38.0f));
    REQUIRE(positionOf(world, id).x == Approx(38.0f));
}

TEST_CASE("A blocked axis still lets the other axis move", "[physics][move]") {
    World world;
    TileGrid grid(20, 20, kTile);
    for (int row = 0; row < 20; ++row) {
        grid.setSolid(3, row, true);
    }

    const Collider box = makeBox(Vec2{10.0f, 6.0f});
    const EntityId id = makeBody(world, Vec2{20.0f, 4.0f}, &box);

    PhysicsWorld physics;
    physics.setTiles(&grid);
    const fa::physics::MoveResult result = physics.move(world, id, Vec2{100.0f, 100.0f});

    REQUIRE(result.hitX);
    REQUIRE_FALSE(result.hitY);
    REQUIRE(result.position.x == Approx(38.0f));
    REQUIRE(result.position.y == Approx(104.0f));
}

TEST_CASE("Vertical movement is blocked by a floor", "[physics][move]") {
    World world;
    TileGrid grid(20, 20, kTile);
    grid.setSolid(0, 10, true);

    const Collider box = makeBox(Vec2{10.0f, 6.0f});
    const EntityId id = makeBody(world, Vec2{4.0f, 100.0f}, &box);

    PhysicsWorld physics;
    physics.setTiles(&grid);
    const fa::physics::MoveResult result = physics.move(world, id, Vec2{0.0f, 100.0f});

    REQUIRE_FALSE(result.hitX);
    REQUIRE(result.hitY);
    // The floor starts at y = 160, the box is 6 tall.
    REQUIRE(result.position.y == Approx(154.0f));
}

TEST_CASE("A fast move is split so it cannot tunnel", "[physics][move]") {
    World world;
    TileGrid grid(40, 20, kTile);
    grid.setSolid(10, 0, true);

    const Collider box = makeBox(Vec2{10.0f, 6.0f});
    const EntityId id = makeBody(world, Vec2{0.0f, 4.0f}, &box);

    PhysicsWorld physics;
    physics.setTiles(&grid);
    const fa::physics::MoveResult result = physics.move(world, id, Vec2{400.0f, 0.0f});

    // 400 px split into 4 px sweeps.
    REQUIRE(result.substeps == 100);
    REQUIRE(result.hitX);
    REQUIRE(result.position.x == Approx(150.0f));
}

TEST_CASE("A trigger collider does not block movement", "[physics][move]") {
    World world;
    Collider trigger = makeBox(Vec2{10.0f, 6.0f},
                               CollisionLayer::Trigger,
                               fa::physics::maskOf(CollisionLayer::Player) | CollisionLayer::World);
    trigger.isTrigger = true;
    static_cast<void>(makeBody(world, Vec2{40.0f, 4.0f}, &trigger));

    const Collider box = makeBox(Vec2{10.0f, 6.0f});
    const EntityId id = makeBody(world, Vec2{20.0f, 4.0f}, &box);

    PhysicsWorld physics;
    const fa::physics::MoveResult result = physics.move(world, id, Vec2{50.0f, 0.0f});
    REQUIRE_FALSE(result.hitX);
    REQUIRE(result.position.x == Approx(70.0f));
}

TEST_CASE("Body blocking honours both colliders' masks", "[physics][move]") {
    using fa::physics::maskOf;

    World world;
    // The mask is mutual: the blocker must also name Player, so the first move below is
    // only ignored because the *player's* mask omits Enemy.
    Collider blocker =
        makeBox(Vec2{10.0f, 6.0f}, CollisionLayer::Enemy, maskOf(CollisionLayer::World) | CollisionLayer::Player);
    static_cast<void>(makeBody(world, Vec2{48.0f, 4.0f}, &blocker));

    Collider box = makeBox(Vec2{10.0f, 6.0f}, CollisionLayer::Player, maskOf(CollisionLayer::World));
    const EntityId id = makeBody(world, Vec2{20.0f, 4.0f}, &box);

    PhysicsWorld physics;
    // Neither mask names the other's layer, so the two pass through each other.
    const fa::physics::MoveResult ignored = physics.move(world, id, Vec2{100.0f, 0.0f});
    REQUIRE_FALSE(ignored.hitX);
    REQUIRE(ignored.position.x == Approx(120.0f));

    // Once the player's mask includes Enemy, the same move is stopped.
    Transform* transform = world.tryGet<Transform>(id);
    REQUIRE(transform != nullptr);
    transform->position = Vec2{20.0f, 4.0f};
    Collider* self = world.tryGet<Collider>(id);
    REQUIRE(self != nullptr);
    self->mask = maskOf(CollisionLayer::World) | CollisionLayer::Enemy;

    const fa::physics::MoveResult blocked = physics.move(world, id, Vec2{100.0f, 0.0f});
    REQUIRE(blocked.hitX);
    REQUIRE(blocked.position.x == Approx(38.0f));
}

TEST_CASE("A collider that omits World flies over tiles", "[physics][move]") {
    using fa::physics::maskOf;

    World world;
    TileGrid grid(20, 20, kTile);
    grid.setSolid(3, 0, true);

    const Collider box = makeBox(Vec2{10.0f, 6.0f}, CollisionLayer::Player, maskOf(CollisionLayer::Enemy));
    const EntityId id = makeBody(world, Vec2{20.0f, 4.0f}, &box);

    PhysicsWorld physics;
    physics.setTiles(&grid);
    const fa::physics::MoveResult result = physics.move(world, id, Vec2{100.0f, 0.0f});
    REQUIRE_FALSE(result.hitX);
    REQUIRE(result.position.x == Approx(120.0f));
}

TEST_CASE("A body already inside a wall can move back out", "[physics][move]") {
    World world;
    TileGrid grid(20, 20, kTile);
    grid.setSolid(3, 0, true);

    // Deliberately overlapping the wall tile [48, 64].
    const Collider box = makeBox(Vec2{10.0f, 6.0f});
    const EntityId id = makeBody(world, Vec2{48.0f, 4.0f}, &box);

    PhysicsWorld physics;
    physics.setTiles(&grid);
    const fa::physics::MoveResult result = physics.move(world, id, Vec2{20.0f, 0.0f});
    // Being stuck is not a permanent state: the overlap is ignored, not enforced.
    REQUIRE(result.position.x == Approx(68.0f));
}

TEST_CASE("Overlap queries filter by area and mask", "[physics][overlap]") {
    using fa::physics::maskOf;

    World world;
    const Collider player = makeBox(Vec2{10.0f, 6.0f}, CollisionLayer::Player, maskOf(CollisionLayer::All));
    const Collider enemy = makeBox(Vec2{10.0f, 6.0f}, CollisionLayer::Enemy, maskOf(CollisionLayer::All));
    const EntityId near = makeBody(world, Vec2{0.0f, 0.0f}, &player);
    const EntityId alsoNear = makeBody(world, Vec2{4.0f, 0.0f}, &enemy);
    static_cast<void>(makeBody(world, Vec2{200.0f, 0.0f}, &player));

    PhysicsWorld physics;
    physics.setTiles(nullptr);
    physics.sync(world);

    const auto all = physics.overlap(world, Rect{-2.0f, -2.0f, 20.0f, 20.0f}, maskOf(CollisionLayer::All));
    REQUIRE(all.size() == 2u);
    REQUIRE(all[0] == near);
    REQUIRE(all[1] == alsoNear);

    const auto playersOnly = physics.overlap(world, Rect{-2.0f, -2.0f, 20.0f, 20.0f}, maskOf(CollisionLayer::Player));
    REQUIRE(playersOnly.size() == 1u);
    REQUIRE(playersOnly[0] == near);
}

TEST_CASE("Circle queries test the box corner exactly", "[physics][overlap]") {
    using fa::physics::maskOf;

    World world;
    const Collider box = makeBox(Vec2{10.0f, 10.0f}, CollisionLayer::Enemy, maskOf(CollisionLayer::All));
    static_cast<void>(makeBody(world, Vec2{10.0f, 10.0f}, &box));

    PhysicsWorld physics;
    physics.sync(world);

    // The nearest corner sits at (10, 10), i.e. 14.14 px from the origin.
    REQUIRE(physics.overlapCircle(world, Vec2{0.0f, 0.0f}, 14.0f, maskOf(CollisionLayer::All)).empty());
    REQUIRE(physics.overlapCircle(world, Vec2{0.0f, 0.0f}, 15.0f, maskOf(CollisionLayer::All)).size() == 1u);
    REQUIRE(physics.overlapCircle(world, Vec2{0.0f, 0.0f}, 0.0f, maskOf(CollisionLayer::All)).empty());
}

TEST_CASE("Raycasts hit tiles at the exact cell boundary", "[physics][ray]") {
    using fa::physics::maskOf;

    World world;
    TileGrid grid(20, 20, kTile);
    grid.setSolid(5, 0, true);

    PhysicsWorld physics;
    physics.setTiles(&grid);
    physics.sync(world);

    const auto hit = physics.raycast(world, Vec2{0.0f, 4.0f}, Vec2{1.0f, 0.0f}, 200.0f, maskOf(CollisionLayer::All));
    REQUIRE(hit.has_value());
    REQUIRE(hit->hitTile);
    REQUIRE(hit->distance == Approx(80.0f));
    REQUIRE(hit->point.x == Approx(80.0f));
    REQUIRE(hit->normal.x == Approx(-1.0f));
    REQUIRE_FALSE(hit->entity.isValid());

    // Too short to reach the wall.
    REQUIRE_FALSE(
        physics.raycast(world, Vec2{0.0f, 4.0f}, Vec2{1.0f, 0.0f}, 40.0f, maskOf(CollisionLayer::All)).has_value());
}

TEST_CASE("Raycasts report a tile the origin is already inside", "[physics][ray]") {
    using fa::physics::maskOf;

    World world;
    TileGrid grid(20, 20, kTile);
    grid.setSolid(1, 1, true);

    PhysicsWorld physics;
    physics.setTiles(&grid);
    physics.sync(world);

    const auto hit = physics.raycast(world, Vec2{20.0f, 20.0f}, Vec2{1.0f, 0.0f}, 50.0f, maskOf(CollisionLayer::All));
    REQUIRE(hit.has_value());
    REQUIRE(hit->hitTile);
    REQUIRE(hit->distance == Approx(0.0f));
}

TEST_CASE("Raycasts return the nearest collider", "[physics][ray]") {
    using fa::physics::maskOf;

    World world;
    const Collider enemy = makeBox(Vec2{10.0f, 10.0f}, CollisionLayer::Enemy, maskOf(CollisionLayer::All));
    const EntityId near = makeBody(world, Vec2{40.0f, 0.0f}, &enemy);
    const EntityId far = makeBody(world, Vec2{80.0f, 0.0f}, &enemy);

    PhysicsWorld physics;
    physics.sync(world);

    const auto hit = physics.raycast(world, Vec2{0.0f, 4.0f}, Vec2{1.0f, 0.0f}, 200.0f, maskOf(CollisionLayer::All));
    REQUIRE(hit.has_value());
    REQUIRE_FALSE(hit->hitTile);
    REQUIRE(hit->entity == near);
    REQUIRE(hit->distance == Approx(40.0f));
    REQUIRE(hit->normal.x == Approx(-1.0f));

    // Restricting the mask to the player layer skips both enemies.
    REQUIRE_FALSE(
        physics.raycast(world, Vec2{0.0f, 4.0f}, Vec2{1.0f, 0.0f}, 200.0f, maskOf(CollisionLayer::Player)).has_value());

    // A ray that starts beyond the near body still finds the far one.
    const auto behind =
        physics.raycast(world, Vec2{60.0f, 4.0f}, Vec2{1.0f, 0.0f}, 200.0f, maskOf(CollisionLayer::All));
    REQUIRE(behind.has_value());
    REQUIRE(behind->entity == far);
    REQUIRE(behind->distance == Approx(20.0f));
}

TEST_CASE("Raycasts reject degenerate input", "[physics][ray]") {
    using fa::physics::maskOf;

    World world;
    PhysicsWorld physics;
    physics.sync(world);

    REQUIRE_FALSE(
        physics.raycast(world, Vec2{0.0f, 0.0f}, Vec2{0.0f, 0.0f}, 10.0f, maskOf(CollisionLayer::All)).has_value());
    REQUIRE_FALSE(
        physics.raycast(world, Vec2{0.0f, 0.0f}, Vec2{1.0f, 0.0f}, 0.0f, maskOf(CollisionLayer::All)).has_value());
    // A long direction vector is normalised, so distance is still in world pixels.
    TileGrid grid(20, 20, kTile);
    grid.setSolid(2, 0, true);
    physics.setTiles(&grid);
    physics.sync(world);
    const auto hit = physics.raycast(world, Vec2{0.0f, 4.0f}, Vec2{500.0f, 0.0f}, 200.0f, maskOf(CollisionLayer::All));
    REQUIRE(hit.has_value());
    REQUIRE(hit->distance == Approx(32.0f));
}

TEST_CASE("Step integrates velocity and stops at geometry", "[physics][step]") {
    World world;
    TileGrid grid(20, 20, kTile);
    grid.setSolid(5, 0, true);

    const Collider box = makeBox(Vec2{10.0f, 6.0f});
    const Velocity velocity{Vec2{60.0f, 0.0f}};
    const EntityId id = makeBody(world, Vec2{0.0f, 4.0f}, &box, &velocity);

    PhysicsWorld physics;
    physics.setTiles(&grid);

    constexpr float dt = 1.0f / 60.0f;
    for (int step = 0; step < 5; ++step) {
        physics.step(world, dt);
    }
    REQUIRE(positionOf(world, id).x == Approx(5.0f));

    // Run long enough to reach the wall at x = 80 and stop flush.
    for (int step = 0; step < 200; ++step) {
        physics.step(world, dt);
    }
    REQUIRE(positionOf(world, id).x == Approx(70.0f));
    REQUIRE(physics.stats().blockedMoves > 0u);
}

TEST_CASE("A zero delta step only refreshes triggers", "[physics][step]") {
    World world;
    const Collider box = makeBox(Vec2{10.0f, 6.0f});
    static_cast<void>(makeBody(world, Vec2{0.0f, 0.0f}, &box));

    PhysicsWorld physics;
    physics.step(world, 0.0f);
    REQUIRE(physics.stats().steps == 1u);
    REQUIRE(physics.stats().moves == 0u);
    REQUIRE(physics.triggerEvents().empty());
}

TEST_CASE("Triggers report entering and leaving", "[physics][triggers]") {
    using fa::physics::maskOf;

    World world;
    Collider trigger =
        makeBox(Vec2{4.0f, 4.0f}, CollisionLayer::Trigger, maskOf(CollisionLayer::Player) | CollisionLayer::World);
    trigger.isTrigger = true;
    const EntityId triggerId = makeBody(world, Vec2{10.0f, 4.0f}, &trigger);

    Collider box =
        makeBox(Vec2{4.0f, 4.0f}, CollisionLayer::Player, maskOf(CollisionLayer::World) | CollisionLayer::Trigger);
    const Velocity velocity{Vec2{60.0f, 0.0f}};
    const EntityId mover = makeBody(world, Vec2{0.0f, 4.0f}, &box, &velocity);

    PhysicsWorld physics;
    physics.step(world, 0.0f);
    REQUIRE(physics.triggerEvents().empty());

    constexpr float dt = 1.0f / 60.0f;
    std::size_t begins = 0;
    std::size_t ends = 0;
    for (int step = 0; step < 30; ++step) {
        physics.step(world, dt);
        for (const TriggerEvent& event : physics.triggerEvents()) {
            REQUIRE(event.trigger == triggerId);
            REQUIRE(event.other == mover);
            if (event.phase == TriggerPhase::Began) {
                ++begins;
            } else {
                ++ends;
            }
        }
    }
    REQUIRE(begins == 1u);
    REQUIRE(ends == 1u);
    REQUIRE(physics.stats().triggerBegins == 1u);
    REQUIRE(physics.stats().triggerEnds == 1u);
    // The body has driven past the trigger, so no pair is active any more.
    REQUIRE(physics.triggerEvents().empty());
}

TEST_CASE("A trigger ignores layers that do not mask it", "[physics][triggers]") {
    using fa::physics::maskOf;

    World world;
    Collider trigger = makeBox(Vec2{40.0f, 40.0f}, CollisionLayer::Trigger, maskOf(CollisionLayer::Player));
    trigger.isTrigger = true;
    static_cast<void>(makeBody(world, Vec2{0.0f, 0.0f}, &trigger));

    // An enemy that does not mask Trigger.
    const Collider enemy = makeBox(Vec2{4.0f, 4.0f}, CollisionLayer::Enemy, maskOf(CollisionLayer::World));
    static_cast<void>(makeBody(world, Vec2{8.0f, 8.0f}, &enemy));

    PhysicsWorld physics;
    physics.step(world, 0.0f);
    REQUIRE(physics.triggerEvents().empty());
    REQUIRE(physics.stats().triggerBodies == 1u);
}

TEST_CASE("Debug shapes cover colliders and solid tiles", "[physics][debug]") {
    World world;
    const Collider box = makeBox(Vec2{10.0f, 6.0f});
    Collider trigger = makeBox(Vec2{8.0f, 8.0f}, CollisionLayer::Trigger);
    trigger.isTrigger = true;
    static_cast<void>(makeBody(world, Vec2{0.0f, 0.0f}, &box));
    static_cast<void>(makeBody(world, Vec2{20.0f, 0.0f}, &trigger));

    TileGrid grid(4, 4, kTile);
    grid.setSolid(0, 0, true);
    grid.setSolid(3, 3, true);

    PhysicsWorld physics;
    physics.setTiles(&grid);

    std::vector<DebugShape> shapes;
    physics.debugDraw(world, shapes);
    REQUIRE(shapes.size() == 4u);

    // A provider without a finite extent contributes no tile shapes.
    TileGrid unbounded;
    PhysicsWorld second;
    second.setTiles(&unbounded);
    std::vector<DebugShape> onlyBodies;
    second.debugDraw(world, onlyBodies);
    REQUIRE(onlyBodies.size() == 2u);
}

TEST_CASE("Stats count queries and moves", "[physics][stats]") {
    using fa::physics::maskOf;

    World world;
    const Collider box = makeBox(Vec2{10.0f, 6.0f}, CollisionLayer::Player, maskOf(CollisionLayer::All));
    const EntityId id = makeBody(world, Vec2{0.0f, 0.0f}, &box);

    PhysicsWorld physics;
    physics.sync(world);
    static_cast<void>(physics.move(world, id, Vec2{1.0f, 0.0f}));
    static_cast<void>(physics.overlap(world, Rect{0.0f, 0.0f, 4.0f, 4.0f}, maskOf(CollisionLayer::All)));
    static_cast<void>(physics.overlapCircle(world, Vec2{0.0f, 0.0f}, 4.0f, maskOf(CollisionLayer::All)));
    static_cast<void>(physics.raycast(world, Vec2{0.0f, 0.0f}, Vec2{1.0f, 0.0f}, 10.0f, maskOf(CollisionLayer::All)));

    const PhysicsStats stats = physics.stats();
    REQUIRE(stats.moves == 1u);
    REQUIRE(stats.blockedMoves == 0u);
    REQUIRE(stats.overlapQueries == 2u);
    REQUIRE(stats.rayQueries == 1u);
}

TEST_CASE("Clearing a physics world resets its state", "[physics][stats]") {
    World world;
    const Collider box = makeBox(Vec2{10.0f, 6.0f});
    static_cast<void>(makeBody(world, Vec2{0.0f, 0.0f}, &box));

    PhysicsWorld physics;
    physics.sync(world);
    REQUIRE(physics.stats().bodies == 1u);

    physics.clear();
    REQUIRE(physics.stats().bodies == 0u);
    REQUIRE(physics.stats().cells == 0u);
    REQUIRE(physics.stats().steps == 0u);
    REQUIRE(physics.triggerEvents().empty());
}

TEST_CASE("PhysicsSystem drives the world from a fixed step", "[physics][system]") {
    BasicScene scene("physics");
    auto system = std::make_unique<PhysicsSystem>(nullptr, 64.0f);
    PhysicsSystem* raw = system.get();
    REQUIRE(raw->name() == "PhysicsSystem");
    scene.systems().add(std::move(system));

    const Collider box = makeBox(Vec2{10.0f, 6.0f});
    const Velocity velocity{Vec2{120.0f, -60.0f}};
    const EntityId id = makeBody(scene.world(), Vec2{0.0f, 100.0f}, &box, &velocity);

    fa::time::FrameContext context;
    scene.onFixedUpdate(context);
    REQUIRE(positionOf(scene.world(), id).x == Approx(2.0f));
    REQUIRE(positionOf(scene.world(), id).y == Approx(99.0f));
    REQUIRE(raw->physics().stats().steps == 1u);
}

TEST_CASE("Identical worlds produce identical motion", "[physics][determinism]") {
    auto run = [](float& endX, std::size_t& blockers) {
        World world;
        TileGrid grid(40, 20, kTile);
        for (int row = 0; row < 20; ++row) {
            grid.setSolid(10, row, true);
        }
        for (int index = 0; index < 20; ++index) {
            const Collider box = makeBox(Vec2{10.0f, 6.0f});
            const Velocity velocity{Vec2{60.0f + static_cast<float>(index), 0.0f}};
            static_cast<void>(makeBody(world, Vec2{0.0f, static_cast<float>(index) * 12.0f}, &box, &velocity));
        }

        PhysicsWorld physics;
        physics.setTiles(&grid);
        for (int step = 0; step < 120; ++step) {
            physics.step(world, 1.0f / 60.0f);
        }
        endX = positionOf(world, EntityId{20, 1}).x;
        blockers = static_cast<std::size_t>(physics.stats().blockedMoves);
    };

    float firstX = 0.0f;
    std::size_t firstBlockers = 0;
    run(firstX, firstBlockers);
    float secondX = 0.0f;
    std::size_t secondBlockers = 0;
    run(secondX, secondBlockers);

    REQUIRE(firstX == secondX);
    REQUIRE(firstBlockers == secondBlockers);
    REQUIRE(firstBlockers > 0u);
}

TEST_CASE("A hundred bodies step without disturbing each other's order", "[physics][scale]") {
    World world;
    TileGrid grid(64, 64, kTile);
    grid.setBorderSolid(true);

    std::vector<EntityId> ids;
    for (int index = 0; index < 100; ++index) {
        const Collider box = makeBox(Vec2{8.0f, 6.0f});
        const Velocity velocity{Vec2{30.0f, 0.0f}};
        ids.push_back(makeBody(world, Vec2{32.0f, 32.0f + static_cast<float>(index % 20) * 10.0f}, &box, &velocity));
    }

    PhysicsWorld physics;
    physics.setTiles(&grid);
    for (int step = 0; step < 10; ++step) {
        physics.step(world, 1.0f / 60.0f);
    }

    REQUIRE(physics.stats().bodies == 100u);
    for (const EntityId id : ids) {
        REQUIRE(positionOf(world, id).x > 32.0f);
    }
}
