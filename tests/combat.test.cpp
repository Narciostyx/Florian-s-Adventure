/// \file
/// \brief Tests for M20: damage, dodge i-frames, attack hitboxes and hit dedup,
/// projectiles, knockback and status effects. Uses a real PhysicsWorld but no
/// renderer or input device.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <utility>

#include <fa/combat/CombatService.hpp>
#include <fa/combat/CombatTypes.hpp>
#include <fa/core/Math.hpp>
#include <fa/physics/Collider.hpp>
#include <fa/physics/CollisionLayer.hpp>
#include <fa/physics/PhysicsWorld.hpp>
#include <fa/player/PlayerController.hpp>
#include <fa/player/PlayerTypes.hpp>
#include <fa/scene/Components.hpp>
#include <fa/scene/World.hpp>

namespace {

using Catch::Approx;

using fa::combat::AttackHitbox;
using fa::combat::CombatService;
using fa::combat::Damage;
using fa::combat::DamageType;
using fa::combat::LootSource;
using fa::combat::ProjectileDesc;
using fa::combat::StatusEffect;
using fa::combat::StatusKind;
using fa::physics::CollisionLayer;
using fa::physics::PhysicsWorld;
using fa::scene::EntityId;

EntityId makeTransformEntity(fa::scene::World& world, fa::core::Vec2 position) {
    const EntityId id = world.create();
    fa::scene::Transform transform;
    transform.position = position;
    static_cast<void>(world.add<fa::scene::Transform>(id, transform));
    return id;
}

EntityId makeTarget(fa::scene::World& world, fa::core::Vec2 position, int health, std::string lootTable) {
    const EntityId id = makeTransformEntity(world, position);
    fa::physics::Collider collider;
    collider.size = fa::core::Vec2{16.0f, 16.0f};
    collider.layer = CollisionLayer::Enemy;
    collider.mask = fa::physics::maskOf(CollisionLayer::Player);
    static_cast<void>(world.add<fa::physics::Collider>(id, collider));

    fa::player::Health hp;
    hp.current = health;
    hp.max = health;
    static_cast<void>(world.add<fa::player::Health>(id, hp));

    LootSource loot;
    loot.lootTable = std::move(lootTable);
    static_cast<void>(world.add<LootSource>(id, loot));
    return id;
}

EntityId makePlayer(fa::scene::World& world, fa::core::Vec2 position) {
    const EntityId id = makeTransformEntity(world, position);
    fa::player::PlayerController::attach(world, id);
    fa::player::Health hp;
    hp.current = 100;
    hp.max = 100;
    static_cast<void>(world.add<fa::player::Health>(id, hp));
    return id;
}

}  // namespace

TEST_CASE("CombatService applies damage and records death", "[combat]") {
    fa::scene::World world;
    PhysicsWorld physics;
    CombatService service(physics);

    const EntityId target = makeTarget(world, {0.0f, 0.0f}, 20, "loot.slime");
    Damage damage;
    damage.amount = 10;

    CHECK(service.applyDamage(world, target, damage));
    CHECK(world.tryGet<fa::player::Health>(target)->current == 10);
    REQUIRE(service.damageEvents().size() == 1);
    CHECK_FALSE(service.damageEvents()[0].killed);

    CHECK(service.applyDamage(world, target, damage));
    CHECK(world.tryGet<fa::player::Health>(target)->current == 0);
    REQUIRE(service.deathEvents().size() == 1);
    CHECK(service.deathEvents()[0].lootTable == "loot.slime");
    CHECK(service.stats().kills == 1);
    CHECK(service.stats().damageDealt == 20);

    CHECK_FALSE(service.applyDamage(world, target, damage));
    CHECK(service.damageEvents().size() == 2);
    CHECK(service.deathEvents().size() == 1);
}

TEST_CASE("CombatService respects dodge invulnerability", "[combat]") {
    fa::scene::World world;
    PhysicsWorld physics;
    CombatService service(physics);

    const EntityId player = makePlayer(world, {0.0f, 0.0f});
    fa::player::DodgeState* dodge = world.tryGet<fa::player::DodgeState>(player);
    REQUIRE(dodge != nullptr);
    dodge->timeLeft = fa::player::PlayerTuning{}.dodgeDuration - 0.10f;

    Damage damage;
    damage.amount = 10;
    CHECK_FALSE(service.applyDamage(world, player, damage));
    CHECK(world.tryGet<fa::player::Health>(player)->current == 100);

    dodge->timeLeft = 0.0f;
    CHECK(service.applyDamage(world, player, damage));
    CHECK(world.tryGet<fa::player::Health>(player)->current == 90);
}

TEST_CASE("CombatService attack hitbox hits once per instance", "[combat]") {
    fa::scene::World world;
    PhysicsWorld physics;
    CombatService service(physics);

    const EntityId attacker = makeTransformEntity(world, {0.0f, 0.0f});
    const EntityId target = makeTarget(world, {10.0f, 0.0f}, 20, "");
    physics.sync(world);

    AttackHitbox hitbox;
    hitbox.offset = fa::core::Vec2{0.0f, 0.0f};
    hitbox.size = fa::core::Vec2{20.0f, 20.0f};
    hitbox.activeFrom = 0.05f;
    hitbox.activeTo = 0.10f;
    hitbox.damage = 5;
    hitbox.targetMask = fa::physics::maskOf(CollisionLayer::Enemy);
    service.spawnAttack(world, attacker, hitbox);

    service.update(world, 0.01f);
    CHECK(world.tryGet<fa::player::Health>(target)->current == 20);

    service.update(world, 0.05f);
    CHECK(world.tryGet<fa::player::Health>(target)->current == 15);

    service.update(world, 0.01f);
    CHECK(world.tryGet<fa::player::Health>(target)->current == 15);

    service.update(world, 0.05f);
    CHECK(world.tryGet<fa::player::Health>(target)->current == 15);
    CHECK(service.stats().attacks == 1);
    CHECK(service.stats().attackHits == 1);
}

TEST_CASE("CombatService projectiles travel and hit", "[combat]") {
    fa::scene::World world;
    PhysicsWorld physics;
    CombatService service(physics);

    const EntityId attacker = makeTransformEntity(world, {0.0f, 0.0f});
    const EntityId target = makeTarget(world, {40.0f, 0.0f}, 20, "");
    physics.sync(world);

    ProjectileDesc desc;
    desc.position = fa::core::Vec2{0.0f, 0.0f};
    desc.velocity = fa::core::Vec2{100.0f, 0.0f};
    desc.lifetime = 2.0f;
    desc.damage = 5;
    desc.targetMask = fa::physics::maskOf(CollisionLayer::Enemy);
    desc.owner = attacker;
    const EntityId projectile = service.spawnProjectile(world, desc);
    REQUIRE(projectile.isValid());

    service.update(world, 0.1f);
    CHECK(world.isAlive(projectile));
    CHECK(world.tryGet<fa::player::Health>(target)->current == 20);

    service.update(world, 0.5f);
    CHECK_FALSE(world.isAlive(projectile));
    CHECK(world.tryGet<fa::player::Health>(target)->current == 15);
    CHECK(service.stats().projectileHits == 1);
    REQUIRE(service.projectileEvents().size() == 1);
    CHECK_FALSE(service.projectileEvents()[0].hitTile);
}

TEST_CASE("CombatService projectiles expire without a target", "[combat]") {
    fa::scene::World world;
    PhysicsWorld physics;
    CombatService service(physics);

    const EntityId attacker = makeTransformEntity(world, {0.0f, 0.0f});
    ProjectileDesc desc;
    desc.velocity = fa::core::Vec2{100.0f, 0.0f};
    desc.lifetime = 0.15f;
    desc.damage = 5;
    desc.owner = attacker;
    const EntityId projectile = service.spawnProjectile(world, desc);
    REQUIRE(projectile.isValid());

    service.update(world, 0.1f);
    CHECK(world.isAlive(projectile));
    service.update(world, 0.1f);
    CHECK_FALSE(world.isAlive(projectile));
    CHECK(service.stats().projectileHits == 0);
}

TEST_CASE("CombatService status effects tick and expire", "[combat]") {
    fa::scene::World world;
    PhysicsWorld physics;
    CombatService service(physics);

    const EntityId target = makeTarget(world, {0.0f, 0.0f}, 20, "");
    StatusEffect poison;
    poison.kind = StatusKind::Poison;
    poison.duration = 1.0f;
    poison.magnitude = 5.0f;
    CHECK(service.applyStatus(world, target, poison));
    CHECK(service.stats().statusApplications == 1);

    service.update(world, 0.25f);
    CHECK(world.tryGet<fa::player::Health>(target)->current == 19);
    CHECK(service.stats().statusTicks == 1);

    service.update(world, 1.0f);
    CHECK(world.tryGet<fa::player::Health>(target)->current == 14);
    CHECK(service.stats().statusTicks == 2);
    REQUIRE(service.statusEvents().size() == 2);
    CHECK_FALSE(service.statusEvents()[0].expired);
    CHECK(service.statusEvents()[1].expired);

    StatusEffect slow;
    slow.kind = StatusKind::Slow;
    slow.duration = 1.0f;
    slow.magnitude = 0.5f;
    CHECK(service.applyStatus(world, target, slow));
    CHECK(service.slowFactor(world, target) == Approx(0.5f));
    service.update(world, 1.0f);
    CHECK(service.slowFactor(world, target) == Approx(1.0f));
}

TEST_CASE("CombatService knockback moves through PhysicsWorld", "[combat]") {
    fa::scene::World world;
    PhysicsWorld physics;
    CombatService service(physics);

    const EntityId target = makeTarget(world, {0.0f, 0.0f}, 20, "");
    Damage damage;
    damage.amount = 1;
    damage.knockback = fa::core::Vec2{12.0f, 0.0f};
    CHECK(service.applyDamage(world, target, damage));
    CHECK(world.tryGet<fa::scene::Transform>(target)->position.x == Approx(12.0f));
}
