/// \file
/// \brief Tests for M21: A*, line of sight, perception, chase/attack, return,
/// flee, patrol and stats. Uses a recording combat service; no GL or frame loop.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "fa/ai/AiService.hpp"
#include "fa/ai/AiTypes.hpp"
#include "fa/combat/CombatService.hpp"
#include "fa/physics/PhysicsWorld.hpp"
#include "fa/player/PlayerTypes.hpp"
#include "fa/scene/Components.hpp"
#include "fa/scene/World.hpp"
#include "fa/tilemap/Tilemap.hpp"

namespace {

using Catch::Approx;

using fa::ai::AiAgent;
using fa::ai::AiService;
using fa::ai::AiState;
using fa::ai::AiTargetTag;
using fa::ai::BehaviorProfile;
using fa::physics::PhysicsWorld;
using fa::player::Health;
using fa::scene::EntityId;
using fa::scene::Transform;
using fa::scene::World;
using fa::tilemap::TileCoord;
using fa::tilemap::TileDef;
using fa::tilemap::TileId;
using fa::tilemap::TileLayer;
using fa::tilemap::Tilemap;

constexpr std::uint16_t kGrassTile = 1;
constexpr std::uint16_t kWallTile = 2;

Tilemap makeMap() {
    Tilemap map;
    REQUIRE(map.reset(32, 32, 16).isOk());

    TileDef grass;
    grass.id = TileId{kGrassTile};
    grass.name = "grass";
    grass.walkable = true;
    REQUIRE(map.addTileDef(grass));

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

void setWall(Tilemap& map, int x, int y) {
    REQUIRE(map.setTile(x, y, 0, TileId{kWallTile}));
}

EntityId makeAgent(World& world, fa::core::Vec2 position, const BehaviorProfile& profile = {}) {
    const EntityId id = world.create();
    Transform transform;
    transform.position = position;
    static_cast<void>(world.add<Transform>(id, transform));
    AiAgent agent;
    agent.home =
        fa::core::Vec2{std::floor(position.x / 16.0f) * 16.0f + 8.0f, std::floor(position.y / 16.0f) * 16.0f + 8.0f};
    agent.profile = profile;
    static_cast<void>(world.add<AiAgent>(id, agent));
    return id;
}

EntityId makeTarget(World& world, fa::core::Vec2 position, int health = 100) {
    const EntityId id = world.create();
    Transform transform;
    transform.position = position;
    static_cast<void>(world.add<Transform>(id, transform));
    static_cast<void>(world.add<AiTargetTag>(id, AiTargetTag{}));
    static_cast<void>(world.add<Health>(id, Health{health, health}));
    return id;
}

fa::core::Vec2 positionOf(const World& world, EntityId id) {
    const Transform* transform = world.tryGet<Transform>(id);
    return transform != nullptr ? transform->position : fa::core::Vec2{};
}

void updateSteps(AiService& service, World& world, int steps) {
    for (int index = 0; index < steps; ++index) {
        service.update(world, 1.0f / 60.0f);
    }
}

class RecordingCombat final : public fa::combat::ICombatService {
public:
    bool applyDamage(World&, EntityId, const fa::combat::Damage&) override { return true; }

    void spawnAttack(World&, EntityId owner, const fa::combat::AttackHitbox& hitbox) override {
        ++attacks;
        lastOwner = owner;
        lastHitbox = hitbox;
    }

    EntityId spawnProjectile(World&, const fa::combat::ProjectileDesc&) override { return EntityId{}; }
    bool applyStatus(World&, EntityId, const fa::combat::StatusEffect&) override { return true; }
    void update(World&, float) override {}

    int attacks = 0;
    EntityId lastOwner{};
    fa::combat::AttackHitbox lastHitbox;
};

BehaviorProfile directProfile() {
    BehaviorProfile profile;
    profile.perceptionIntervalSteps = 1;
    profile.sightRadius = 400.0f;
    profile.attackRange = 20.0f;
    profile.moveSpeed = 60.0f;
    profile.repathSeconds = 0.1f;
    profile.loseTargetSeconds = 0.05f;
    return profile;
}

}  // namespace

TEST_CASE("A* routes around a wall", "[ai]") {
    Tilemap map = makeMap();
    for (int y = 1; y <= 7; ++y) {
        setWall(map, 4, y);
    }

    PhysicsWorld physics;
    AiService service(map, physics);
    std::vector<TileCoord> path;
    REQUIRE(service.findPath(TileCoord{1, 4}, TileCoord{7, 4}, path));
    REQUIRE_FALSE(path.empty());
    CHECK(path.back() == TileCoord{7, 4});

    bool crossedAroundWall = false;
    for (const TileCoord& cell : path) {
        CHECK_FALSE(map.isSolidCell(cell.x, cell.y));
        if (cell.y == 0 || cell.y == 8) {
            crossedAroundWall = true;
        }
    }
    CHECK(crossedAroundWall);
}

TEST_CASE("A* rejects an unreachable goal", "[ai]") {
    Tilemap map = makeMap();
    setWall(map, 9, 10);
    setWall(map, 11, 10);
    setWall(map, 10, 9);
    setWall(map, 10, 11);

    PhysicsWorld physics;
    AiService service(map, physics);
    std::vector<TileCoord> path;
    CHECK_FALSE(service.findPath(TileCoord{1, 1}, TileCoord{10, 10}, path));
    CHECK(path.empty());
}

TEST_CASE("Line of sight is blocked by solid cells", "[ai]") {
    Tilemap map = makeMap();
    setWall(map, 8, 6);

    CHECK_FALSE(AiService::hasLineOfSight(map, TileCoord{6, 6}, TileCoord{11, 6}));
    CHECK(AiService::hasLineOfSight(map, TileCoord{6, 5}, TileCoord{11, 5}));
    CHECK(AiService::hasLineOfSight(map, TileCoord{6, 6}, TileCoord{6, 6}));
}

TEST_CASE("Perception finds a visible target in range", "[ai]") {
    Tilemap map = makeMap();
    PhysicsWorld physics;
    RecordingCombat combat;
    AiService service(map, physics, &combat);
    World world;

    BehaviorProfile profile = directProfile();
    profile.sightRadius = 160.0f;
    const EntityId agent = makeAgent(world, fa::core::Vec2{100.0f, 100.0f}, profile);
    const EntityId target = makeTarget(world, fa::core::Vec2{180.0f, 100.0f});

    service.update(world, 1.0f / 60.0f);
    const AiAgent* state = world.tryGet<AiAgent>(agent);
    REQUIRE(state != nullptr);
    CHECK(state->target == target);
    CHECK(state->targetVisible);
    CHECK(state->state == AiState::Chase);
    CHECK(service.stats().targetsFound == 1);
}

TEST_CASE("Perception respects range and walls", "[ai]") {
    Tilemap map = makeMap();
    setWall(map, 8, 6);
    PhysicsWorld physics;
    AiService service(map, physics);
    World world;

    BehaviorProfile profile = directProfile();
    profile.sightRadius = 300.0f;
    const EntityId agent = makeAgent(world, fa::core::Vec2{100.0f, 100.0f}, profile);
    static_cast<void>(makeTarget(world, fa::core::Vec2{180.0f, 100.0f}));

    service.update(world, 1.0f / 60.0f);
    const AiAgent* state = world.tryGet<AiAgent>(agent);
    REQUIRE(state != nullptr);
    CHECK_FALSE(state->targetVisible);
}

TEST_CASE("Chase moves the agent toward the target", "[ai]") {
    Tilemap map = makeMap();
    PhysicsWorld physics;
    AiService service(map, physics);
    World world;

    BehaviorProfile profile = directProfile();
    const EntityId agent = makeAgent(world, fa::core::Vec2{100.0f, 100.0f}, profile);
    static_cast<void>(makeTarget(world, fa::core::Vec2{200.0f, 100.0f}));

    const float before = positionOf(world, agent).x;
    updateSteps(service, world, 20);
    const float after = positionOf(world, agent).x;
    CHECK(after > before);
    CHECK(world.tryGet<AiAgent>(agent)->state == AiState::Chase);
    CHECK(service.stats().moves > 0);
    CHECK(service.stats().pathsFound > 0);
}

TEST_CASE("Attack fires at range and respects cooldown", "[ai]") {
    Tilemap map = makeMap();
    PhysicsWorld physics;
    RecordingCombat combat;
    AiService service(map, physics, &combat);
    World world;

    BehaviorProfile profile = directProfile();
    profile.sightRadius = 100.0f;
    profile.attackRange = 30.0f;
    profile.attackDamage = 7;
    profile.attackCooldown = 0.2f;
    const EntityId agent = makeAgent(world, fa::core::Vec2{100.0f, 100.0f}, profile);
    static_cast<void>(makeTarget(world, fa::core::Vec2{115.0f, 100.0f}));

    service.update(world, 1.0f / 60.0f);
    CHECK(world.tryGet<AiAgent>(agent)->state == AiState::Chase);
    service.update(world, 1.0f / 60.0f);
    CHECK(world.tryGet<AiAgent>(agent)->state == AiState::Attack);
    service.update(world, 1.0f / 60.0f);
    CHECK(combat.attacks == 1);
    CHECK(combat.lastOwner == agent);
    CHECK(combat.lastHitbox.damage == 7);
    CHECK(combat.lastHitbox.targetMask != 0);

    updateSteps(service, world, 3);
    CHECK(combat.attacks == 1);
    updateSteps(service, world, 15);
    CHECK(combat.attacks >= 2);
    CHECK(service.stats().attacks >= 2);
}

TEST_CASE("Attack handler intercepts enemy attacks", "[ai]") {
    Tilemap map = makeMap();
    PhysicsWorld physics;
    RecordingCombat combat;
    AiService service(map, physics, &combat);
    World world;

    int hookCalls = 0;
    EntityId hookOwner{};
    fa::combat::AttackHitbox hookHitbox;
    service.setAttackHandler([&](EntityId agent, const fa::combat::AttackHitbox& hitbox) {
        ++hookCalls;
        hookOwner = agent;
        hookHitbox = hitbox;
    });

    BehaviorProfile profile = directProfile();
    profile.sightRadius = 100.0f;
    profile.attackRange = 30.0f;
    profile.attackDamage = 9;
    const EntityId agent = makeAgent(world, fa::core::Vec2{100.0f, 100.0f}, profile);
    static_cast<void>(makeTarget(world, fa::core::Vec2{115.0f, 100.0f}));

    updateSteps(service, world, 3);
    CHECK(hookCalls == 1);
    CHECK(hookOwner == agent);
    CHECK(hookHitbox.damage == 9);
    // The handler owns the hitbox; M21 must not also spawn one itself.
    CHECK(combat.attacks == 0);
    CHECK(service.stats().attacks == 1);
}

TEST_CASE("An agent returns home after losing its target", "[ai]") {
    Tilemap map = makeMap();
    PhysicsWorld physics;
    AiService service(map, physics);
    World world;

    BehaviorProfile profile = directProfile();
    profile.sightRadius = 120.0f;
    profile.moveSpeed = 60.0f;
    const EntityId agent = makeAgent(world, fa::core::Vec2{100.0f, 100.0f}, profile);
    const EntityId target = makeTarget(world, fa::core::Vec2{150.0f, 100.0f});

    service.update(world, 1.0f / 60.0f);
    REQUIRE(world.tryGet<AiAgent>(agent)->target == target);

    Transform* agentTransform = world.tryGet<Transform>(agent);
    REQUIRE(agentTransform != nullptr);
    agentTransform->position = fa::core::Vec2{300.0f, 100.0f};
    Transform* targetTransform = world.tryGet<Transform>(target);
    REQUIRE(targetTransform != nullptr);
    targetTransform->position = fa::core::Vec2{700.0f, 700.0f};

    updateSteps(service, world, 6);
    CHECK(world.tryGet<AiAgent>(agent)->state == AiState::Return);

    updateSteps(service, world, 300);
    CHECK(world.tryGet<AiAgent>(agent)->state == AiState::Idle);
    CHECK(positionOf(world, agent).x == Approx(100.0f).margin(8.0f));
}

TEST_CASE("Low health makes an agent flee", "[ai]") {
    Tilemap map = makeMap();
    PhysicsWorld physics;
    AiService service(map, physics);
    World world;

    BehaviorProfile profile = directProfile();
    profile.fleeHealthRatio = 0.5f;
    const EntityId agent = makeAgent(world, fa::core::Vec2{150.0f, 100.0f}, profile);
    AiAgent* agentState = world.tryGet<AiAgent>(agent);
    REQUIRE(agentState != nullptr);
    agentState->home = fa::core::Vec2{104.0f, 104.0f};
    static_cast<void>(world.add<Health>(agent, Health{10, 100}));
    static_cast<void>(makeTarget(world, fa::core::Vec2{160.0f, 100.0f}));

    service.update(world, 1.0f / 60.0f);
    CHECK(world.tryGet<AiAgent>(agent)->state == AiState::Flee);
}

TEST_CASE("Patrol creates a path without a target", "[ai]") {
    Tilemap map = makeMap();
    PhysicsWorld physics;
    AiService service(map, physics);
    World world;

    BehaviorProfile profile = directProfile();
    profile.patrol = true;
    profile.patrolRadius = 48.0f;
    const EntityId agent = makeAgent(world, fa::core::Vec2{100.0f, 100.0f}, profile);

    service.update(world, 1.0f / 60.0f);
    CHECK(world.tryGet<AiAgent>(agent)->state == AiState::Patrol);
    CHECK(service.stats().pathsFound > 0);
}

TEST_CASE("Ai agent snapshots round-trip persistent fields", "[ai]") {
    AiAgent source;
    source.state = AiState::Chase;
    source.home = fa::core::Vec2{12.0f, 34.0f};
    source.stateTime = 1.5f;
    source.attackCooldown = 0.25f;
    source.loseTimer = 2.0f;
    source.repathTimer = 0.5f;
    source.targetVisible = true;
    source.target = EntityId{7, 1};

    const fa::ai::AiAgentSnapshot snapshot = fa::ai::captureAiAgent(source);
    CHECK(snapshot.state == AiState::Chase);
    CHECK(snapshot.home.x == Approx(12.0f));
    CHECK(snapshot.home.y == Approx(34.0f));
    CHECK(snapshot.targetVisible);

    // The target is scene-local and deliberately not part of the snapshot.
    AiAgent restored;
    restored.target = EntityId{9, 2};
    fa::ai::applyAiAgentSnapshot(restored, snapshot);
    CHECK(restored.state == AiState::Chase);
    CHECK(restored.home.x == Approx(12.0f));
    CHECK(restored.home.y == Approx(34.0f));
    CHECK(restored.stateTime == Approx(1.5f));
    CHECK(restored.attackCooldown == Approx(0.25f));
    CHECK(restored.loseTimer == Approx(2.0f));
    CHECK(restored.repathTimer == Approx(0.5f));
    CHECK(restored.targetVisible);
    CHECK(restored.target == EntityId{9, 2});

    fa::ai::AiAgentSnapshot invalid;
    invalid.state = static_cast<AiState>(255);
    fa::ai::applyAiAgentSnapshot(restored, invalid);
    CHECK(restored.state == AiState::Idle);
}

TEST_CASE("AiService clear resets counters", "[ai]") {
    Tilemap map = makeMap();
    PhysicsWorld physics;
    AiService service(map, physics);
    World world;

    static_cast<void>(makeAgent(world, fa::core::Vec2{100.0f, 100.0f}, directProfile()));
    service.update(world, 1.0f / 60.0f);
    CHECK(service.stats().steps == 1);
    service.clear();
    CHECK(service.stats().steps == 0);
    CHECK(service.stats().agents == 0);
}
