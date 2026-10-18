#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <fa/core/Math.hpp>
#include <fa/physics/CollisionLayer.hpp>
#include <fa/scene/EntityId.hpp>

namespace fa::combat {

/// \brief Damage school. v1 has no resistance table; the tag is carried for events.
enum class DamageType : std::uint8_t {
    Physical = 0,
    Fire,
    Ice,
    Poison,
    Count,
};

/// \brief One damage application.
struct Damage {
    int amount = 0;
    DamageType type = DamageType::Physical;
    scene::EntityId source{};
    /// \brief Applied through PhysicsWorld::move, never by writing Transform directly.
    core::Vec2 knockback{};
};

/// \brief A temporary world-space box used by one attack instance.
struct AttackHitbox {
    /// \brief Top-left relative to the owner's Transform position.
    core::Vec2 offset{};
    core::Vec2 size{24.0f, 20.0f};
    /// \brief Seconds from attack start when the box becomes active.
    float activeFrom = 0.0f;
    float activeTo = 0.1f;
    int damage = 1;
    DamageType type = DamageType::Physical;
    physics::CollisionMask targetMask = physics::maskOf(physics::CollisionLayer::Enemy);
    core::Vec2 knockback{};
    /// \brief Optional caller id for logs; zero lets CombatService assign one.
    std::uint32_t attackId = 0;
};

/// \brief Spawn parameters for a straight-line projectile.
struct ProjectileDesc {
    core::Vec2 position{};
    core::Vec2 velocity{};
    float lifetime = 1.0f;
    int damage = 1;
    DamageType type = DamageType::Physical;
    physics::CollisionMask targetMask = physics::maskOf(physics::CollisionLayer::Enemy);
    scene::EntityId owner{};
};

/// \brief Data component on a projectile entity. The entity also has a Transform.
struct Projectile {
    core::Vec2 velocity{};
    float lifetime = 1.0f;
    int damage = 1;
    DamageType type = DamageType::Physical;
    physics::CollisionMask targetMask = physics::maskOf(physics::CollisionLayer::Enemy);
    scene::EntityId owner{};
};

/// \brief Kind of lingering effect.
enum class StatusKind : std::uint8_t {
    /// \brief Damage over time; magnitude is damage per second.
    Poison = 0,
    /// \brief Movement multiplier; magnitude 0.5 halves speed.
    Slow,
    /// \brief Damage over time; magnitude is damage per second.
    Burn,
    Count,
};

/// \brief One status effect instance.
struct StatusEffect {
    StatusKind kind = StatusKind::Poison;
    float duration = 1.0f;
    float magnitude = 1.0f;
    scene::EntityId source{};
    /// \brief Internal fractional damage accumulator for DoT ticks.
    float accumulated = 0.0f;
};

/// \brief Data component holding active statuses on an entity.
struct CombatStatus {
    std::vector<StatusEffect> effects;
};

/// \brief Optional component naming the loot table M15 should roll on death.
struct LootSource {
    std::string lootTable;
};

/// \brief One applied hit, appended to the service's event buffer.
struct DamageEvent {
    scene::EntityId target{};
    scene::EntityId source{};
    int amount = 0;
    DamageType type = DamageType::Physical;
    bool killed = false;
};

/// \brief One death, appended to the service's event buffer.
struct DeathEvent {
    scene::EntityId entity{};
    scene::EntityId killer{};
    std::string lootTable;
};

/// \brief One projectile collision (entity or wall).
struct ProjectileEvent {
    scene::EntityId projectile{};
    scene::EntityId target{};
    bool hitTile = false;
};

/// \brief One status application or expiry.
struct StatusEvent {
    scene::EntityId target{};
    StatusKind kind = StatusKind::Poison;
    bool expired = false;
};

/// \brief Counters for the smoke summary and M27.
struct CombatStats {
    std::uint64_t attacks = 0;
    std::uint64_t attackHits = 0;
    std::uint64_t damageDealt = 0;
    std::uint64_t kills = 0;
    std::uint64_t projectilesSpawned = 0;
    std::uint64_t projectileHits = 0;
    std::uint64_t projectileWallHits = 0;
    std::uint64_t statusApplications = 0;
    std::uint64_t statusTicks = 0;
};

}  // namespace fa::combat
