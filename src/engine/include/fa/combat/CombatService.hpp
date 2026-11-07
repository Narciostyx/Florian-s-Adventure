#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <fa/combat/CombatTypes.hpp>
#include <fa/player/PlayerTypes.hpp>

namespace fa::physics {
class PhysicsWorld;
}

namespace fa::scene {
class World;
}

namespace fa::combat {

/// \brief Narrow combat entry point used by M16/M21 and the composition root.
class ICombatService {
public:
    virtual ~ICombatService() = default;

    /// \brief Applies damage unless the target is invulnerable, dead, or has no Health.
    /// \return true when at least one point was applied.
    virtual bool applyDamage(scene::World& world, scene::EntityId target, const Damage& damage) = 0;
    /// \brief Registers a temporary melee hitbox owned by \p owner.
    virtual void spawnAttack(scene::World& world, scene::EntityId owner, const AttackHitbox& hitbox) = 0;
    /// \brief Creates a projectile entity. \return its id, or an invalid id when the
    /// description is unusable (no owner, no velocity, non-positive damage/lifetime).
    virtual scene::EntityId spawnProjectile(scene::World& world, const ProjectileDesc& desc) = 0;
    /// \brief Adds or refreshes one status. Returns false for an invalid effect.
    virtual bool applyStatus(scene::World& world, scene::EntityId target, const StatusEffect& effect) = 0;
    /// \brief Advances attacks, projectiles and statuses by one fixed step.
    virtual void update(scene::World& world, float deltaSeconds) = 0;
};

/// \brief Fixed-step combat executor.
///
/// Attacks are temporary hitboxes; the same attack instance hits a target at most
/// once. Projectiles are real entities with `Transform` + `Projectile` and use
/// `PhysicsWorld::raycast`/`move`, so walls stop them. Damage uses
/// `player::PlayerController::isInvulnerable` for dodge i-frames and never writes
/// `Transform::position` directly.
class CombatService final : public ICombatService {
public:
    /// \brief Creates the executor over the physics world; \p tuning supplies the
    /// post-hit invulnerability window.
    explicit CombatService(physics::PhysicsWorld& physics, player::PlayerTuning tuning = {});

    /// \brief Interface overrides; semantics documented on \ref ICombatService. @{
    bool applyDamage(scene::World& world, scene::EntityId target, const Damage& damage) override;
    void spawnAttack(scene::World& world, scene::EntityId owner, const AttackHitbox& hitbox) override;
    scene::EntityId spawnProjectile(scene::World& world, const ProjectileDesc& desc) override;
    bool applyStatus(scene::World& world, scene::EntityId target, const StatusEffect& effect) override;
    void update(scene::World& world, float deltaSeconds) override;
    /// @}

    [[nodiscard]] std::span<const DamageEvent> damageEvents() const noexcept { return m_damageEvents; }
    [[nodiscard]] std::span<const DeathEvent> deathEvents() const noexcept { return m_deathEvents; }
    [[nodiscard]] std::span<const ProjectileEvent> projectileEvents() const noexcept { return m_projectileEvents; }
    [[nodiscard]] std::span<const StatusEvent> statusEvents() const noexcept { return m_statusEvents; }
    /// \brief Clears all four event rings; counters are cumulative and stay.
    void clearEvents() noexcept;

    [[nodiscard]] const CombatStats& stats() const noexcept { return m_stats; }
    /// \brief 1.0 without a slow, otherwise the strongest active slow multiplier.
    [[nodiscard]] float slowFactor(const scene::World& world, scene::EntityId entity) const;

private:
    struct ActiveAttack {
        scene::EntityId owner{};
        AttackHitbox hitbox;
        float elapsed = 0.0f;
        std::vector<scene::EntityId> hit;
    };

    /// \brief Advances live hitboxes: overlap tests, hit-once bookkeeping, expiry.
    void updateAttacks(scene::World& world, float deltaSeconds);
    /// \brief Moves projectiles, resolves wall hits and target hits, expires them.
    void updateProjectiles(scene::World& world, float deltaSeconds);
    /// \brief Ticks DoT/Slow effects and expires them.
    void updateStatuses(scene::World& world, float deltaSeconds);
    /// \brief Marks the target dead, rolls its loot table and records the event.
    void recordDeath(scene::World& world, scene::EntityId entity, scene::EntityId killer);

    physics::PhysicsWorld& m_physics;
    player::PlayerTuning m_tuning;
    std::vector<ActiveAttack> m_attacks;
    std::vector<DamageEvent> m_damageEvents;
    std::vector<DeathEvent> m_deathEvents;
    std::vector<ProjectileEvent> m_projectileEvents;
    std::vector<StatusEvent> m_statusEvents;
    CombatStats m_stats;
    std::uint32_t m_nextAttackId = 1;
};

}  // namespace fa::combat
