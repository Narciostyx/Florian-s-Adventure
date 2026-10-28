#include "fa/combat/CombatService.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

#include <fa/physics/PhysicsWorld.hpp>
#include <fa/player/PlayerController.hpp>
#include <fa/scene/Components.hpp>
#include <fa/scene/World.hpp>

namespace fa::combat {
namespace {

[[nodiscard]] bool isDot(StatusKind kind) noexcept {
    return kind == StatusKind::Poison || kind == StatusKind::Burn;
}

[[nodiscard]] DamageType damageTypeFor(StatusKind kind) noexcept {
    return kind == StatusKind::Poison ? DamageType::Poison : DamageType::Fire;
}

}  // namespace

CombatService::CombatService(physics::PhysicsWorld& physics, player::PlayerTuning tuning)
    : m_physics(physics), m_tuning(tuning) {}

bool CombatService::applyDamage(scene::World& world, scene::EntityId target, const Damage& damage) {
    if (!target.isValid() || damage.amount <= 0) {
        return false;
    }
    if (player::PlayerController::isInvulnerable(world, target, m_tuning)) {
        return false;
    }

    player::Health* health = world.tryGet<player::Health>(target);
    if (health == nullptr || health->current <= 0) {
        return false;
    }

    const int before = health->current;
    health->current = std::max(0, health->current - damage.amount);
    const int applied = before - health->current;
    if (applied <= 0) {
        return false;
    }

    const bool killed = health->current == 0;
    m_damageEvents.push_back(DamageEvent{target, damage.source, applied, damage.type, killed});
    m_stats.damageDealt += static_cast<std::uint64_t>(applied);
    if (killed) {
        recordDeath(world, target, damage.source);
    } else if (damage.knockback.x != 0.0f || damage.knockback.y != 0.0f) {
        static_cast<void>(m_physics.move(world, target, damage.knockback));
    }
    return true;
}

void CombatService::spawnAttack(scene::World& world, scene::EntityId owner, const AttackHitbox& hitbox) {
    if (!owner.isValid() || !world.isAlive(owner) || hitbox.damage <= 0 || hitbox.targetMask == 0) {
        return;
    }
    AttackHitbox clean = hitbox;
    if (clean.activeTo < clean.activeFrom) {
        std::swap(clean.activeFrom, clean.activeTo);
    }
    if (clean.attackId == 0) {
        clean.attackId = m_nextAttackId++;
    }
    m_attacks.push_back(ActiveAttack{owner, clean, 0.0f, {}});
    ++m_stats.attacks;
}

scene::EntityId CombatService::spawnProjectile(scene::World& world, const ProjectileDesc& desc) {
    if (!desc.owner.isValid() || desc.lifetime <= 0.0f || desc.damage <= 0 ||
        (desc.velocity.x == 0.0f && desc.velocity.y == 0.0f)) {
        return scene::EntityId{};
    }

    const scene::EntityId entity = world.create();
    scene::Transform transform;
    transform.position = desc.position;
    static_cast<void>(world.add<scene::Transform>(entity, transform));

    Projectile projectile;
    projectile.velocity = desc.velocity;
    projectile.lifetime = desc.lifetime;
    projectile.damage = desc.damage;
    projectile.type = desc.type;
    projectile.targetMask = desc.targetMask;
    projectile.owner = desc.owner;
    static_cast<void>(world.add<Projectile>(entity, projectile));
    ++m_stats.projectilesSpawned;
    return entity;
}

bool CombatService::applyStatus(scene::World& world, scene::EntityId target, const StatusEffect& effect) {
    if (!target.isValid() || !world.isAlive(target) || effect.duration <= 0.0f || effect.magnitude < 0.0f) {
        return false;
    }

    CombatStatus* status = world.tryGet<CombatStatus>(target);
    if (status == nullptr) {
        static_cast<void>(world.add<CombatStatus>(target, CombatStatus{}));
        status = world.tryGet<CombatStatus>(target);
    }
    if (status == nullptr) {
        return false;
    }

    StatusEffect fresh = effect;
    fresh.accumulated = 0.0f;
    for (StatusEffect& existing : status->effects) {
        if (existing.kind == fresh.kind) {
            existing = std::move(fresh);
            m_statusEvents.push_back(StatusEvent{target, existing.kind, false});
            ++m_stats.statusApplications;
            return true;
        }
    }
    m_statusEvents.push_back(StatusEvent{target, fresh.kind, false});
    status->effects.push_back(std::move(fresh));
    ++m_stats.statusApplications;
    return true;
}

void CombatService::update(scene::World& world, float deltaSeconds) {
    if (!(deltaSeconds > 0.0f)) {
        return;
    }
    updateStatuses(world, deltaSeconds);
    updateAttacks(world, deltaSeconds);
    updateProjectiles(world, deltaSeconds);
}

void CombatService::clearEvents() noexcept {
    m_damageEvents.clear();
    m_deathEvents.clear();
    m_projectileEvents.clear();
    m_statusEvents.clear();
}

float CombatService::slowFactor(const scene::World& world, scene::EntityId entity) const {
    const CombatStatus* status = world.tryGet<CombatStatus>(entity);
    if (status == nullptr) {
        return 1.0f;
    }
    float factor = 1.0f;
    for (const StatusEffect& effect : status->effects) {
        if (effect.kind == StatusKind::Slow && effect.duration > 0.0f) {
            factor = std::min(factor, core::clampf(1.0f - effect.magnitude, 0.1f, 1.0f));
        }
    }
    return factor;
}

void CombatService::updateAttacks(scene::World& world, float deltaSeconds) {
    for (auto it = m_attacks.begin(); it != m_attacks.end();) {
        const float before = it->elapsed;
        it->elapsed += deltaSeconds;

        if (it->elapsed >= it->hitbox.activeFrom && before <= it->hitbox.activeTo) {
            const scene::Transform* transform = world.tryGet<scene::Transform>(it->owner);
            if (transform != nullptr) {
                const core::Rect box = core::makeRect(transform->position.x + it->hitbox.offset.x,
                                                      transform->position.y + it->hitbox.offset.y,
                                                      it->hitbox.size.x,
                                                      it->hitbox.size.y);
                const std::vector<scene::EntityId> targets = m_physics.overlap(world, box, it->hitbox.targetMask);
                for (const scene::EntityId target : targets) {
                    if (target == it->owner) {
                        continue;
                    }
                    if (std::find(it->hit.begin(), it->hit.end(), target) != it->hit.end()) {
                        continue;
                    }
                    Damage damage;
                    damage.amount = it->hitbox.damage;
                    damage.type = it->hitbox.type;
                    damage.source = it->owner;
                    damage.knockback = it->hitbox.knockback;
                    if (applyDamage(world, target, damage)) {
                        ++m_stats.attackHits;
                        it->hit.push_back(target);
                    }
                }
            }
        }

        if (it->elapsed > it->hitbox.activeTo) {
            it = m_attacks.erase(it);
        } else {
            ++it;
        }
    }
}

void CombatService::updateProjectiles(scene::World& world, float deltaSeconds) {
    std::vector<scene::EntityId> ids;
    world.each<scene::Transform, Projectile>(
        [&ids](scene::EntityId id, scene::Transform&, Projectile&) { ids.push_back(id); });

    for (const scene::EntityId id : ids) {
        if (!world.isAlive(id)) {
            continue;
        }
        scene::Transform* transform = world.tryGet<scene::Transform>(id);
        Projectile* projectile = world.tryGet<Projectile>(id);
        if (transform == nullptr || projectile == nullptr) {
            continue;
        }

        const float speed = std::sqrt(projectile->velocity.x * projectile->velocity.x +
                                      projectile->velocity.y * projectile->velocity.y);
        if (speed <= 0.0f) {
            world.destroy(id);
            continue;
        }
        const core::Vec2 direction{projectile->velocity.x / speed, projectile->velocity.y / speed};
        const float distance = speed * deltaSeconds;
        const std::optional<physics::RayHit> hit =
            m_physics.raycast(world, transform->position, direction, distance, projectile->targetMask);

        if (hit.has_value()) {
            if (hit->hitTile) {
                ++m_stats.projectileWallHits;
                m_projectileEvents.push_back(ProjectileEvent{id, {}, true});
                world.destroy(id);
                continue;
            }
            if (hit->entity.isValid()) {
                Damage damage;
                damage.amount = projectile->damage;
                damage.type = projectile->type;
                damage.source = projectile->owner;
                damage.knockback = core::Vec2{direction.x * 8.0f, direction.y * 8.0f};
                if (applyDamage(world, hit->entity, damage)) {
                    ++m_stats.projectileHits;
                }
                m_projectileEvents.push_back(ProjectileEvent{id, hit->entity, false});
                world.destroy(id);
                continue;
            }
        }

        const physics::MoveResult moved = m_physics.move(
            world, id, core::Vec2{projectile->velocity.x * deltaSeconds, projectile->velocity.y * deltaSeconds});
        if (moved.hitX || moved.hitY) {
            ++m_stats.projectileWallHits;
            m_projectileEvents.push_back(ProjectileEvent{id, {}, true});
            world.destroy(id);
            continue;
        }

        projectile->lifetime -= deltaSeconds;
        if (projectile->lifetime <= 0.0f) {
            world.destroy(id);
        }
    }
}

void CombatService::updateStatuses(scene::World& world, float deltaSeconds) {
    std::vector<scene::EntityId> ids;
    world.each<CombatStatus>([&ids](scene::EntityId id, CombatStatus&) { ids.push_back(id); });

    for (const scene::EntityId id : ids) {
        if (!world.isAlive(id)) {
            continue;
        }
        CombatStatus* status = world.tryGet<CombatStatus>(id);
        if (status == nullptr) {
            continue;
        }

        for (auto it = status->effects.begin(); it != status->effects.end();) {
            if (isDot(it->kind)) {
                it->accumulated += it->magnitude * deltaSeconds;
                const int ticks = static_cast<int>(it->accumulated);
                if (ticks > 0) {
                    it->accumulated -= static_cast<float>(ticks);
                    Damage damage;
                    damage.amount = ticks;
                    damage.type = damageTypeFor(it->kind);
                    damage.source = it->source;
                    static_cast<void>(applyDamage(world, id, damage));
                    ++m_stats.statusTicks;
                }
            }

            it->duration -= deltaSeconds;
            if (it->duration <= 0.0f) {
                m_statusEvents.push_back(StatusEvent{id, it->kind, true});
                it = status->effects.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void CombatService::recordDeath(scene::World& world, scene::EntityId entity, scene::EntityId killer) {
    DeathEvent event;
    event.entity = entity;
    event.killer = killer;
    if (const LootSource* loot = world.tryGet<LootSource>(entity); loot != nullptr) {
        event.lootTable = loot->lootTable;
    }
    m_deathEvents.push_back(std::move(event));
    ++m_stats.kills;
}

}  // namespace fa::combat
