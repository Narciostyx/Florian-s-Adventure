#include "fa/player/PlayerController.hpp"

#include "fa/physics/PhysicsWorld.hpp"
#include "fa/player/PlayerError.hpp"
#include "fa/scene/Components.hpp"
#include "fa/scene/World.hpp"

#include <algorithm>
#include <cmath>

namespace fa::player {

namespace {

/// \brief Normalizes any non-zero move vector so walk/run speeds are direction-independent.
[[nodiscard]] core::Vec2 normalizeMove(core::Vec2 move) noexcept {
    const float lengthSquared = move.x * move.x + move.y * move.y;
    if (lengthSquared <= 0.0f) {
        return core::Vec2{0.0f, 0.0f};
    }
    const float inverse = 1.0f / std::sqrt(lengthSquared);
    return core::Vec2{move.x * inverse, move.y * inverse};
}

/// \brief Moves `current` toward `target` by at most `maxDelta` pixels/second.
[[nodiscard]] core::Vec2 approachVector(core::Vec2 current, core::Vec2 target, float maxDelta) noexcept {
    if (maxDelta <= 0.0f) {
        return current;
    }
    const float dx = target.x - current.x;
    const float dy = target.y - current.y;
    const float distance = std::sqrt(dx * dx + dy * dy);
    if (distance <= maxDelta || distance <= 0.0f) {
        return target;
    }
    const float scale = maxDelta / distance;
    return core::Vec2{current.x + dx * scale, current.y + dy * scale};
}

}  // namespace

PlayerController::PlayerController(PlayerTuning tuning) : m_tuning(tuning) {}

void PlayerController::attach(scene::World& world, scene::EntityId player, Stamina stamina, Facing facing) {
    if (!world.has<PlayerTag>(player)) {
        static_cast<void>(world.add<PlayerTag>(player, PlayerTag{}));
    }
    if (!world.has<FacingDir>(player)) {
        FacingDir dir;
        dir.value = facing;
        static_cast<void>(world.add<FacingDir>(player, dir));
    }
    if (!world.has<PlayerStateComp>(player)) {
        static_cast<void>(world.add<PlayerStateComp>(player, PlayerStateComp{}));
    }
    if (!world.has<Stamina>(player)) {
        static_cast<void>(world.add<Stamina>(player, stamina));
    }
    if (!world.has<DodgeState>(player)) {
        static_cast<void>(world.add<DodgeState>(player, DodgeState{}));
    }
}

void PlayerController::setState(PlayerStateComp& state, PlayerState next) {
    if (state.state != next) {
        state.state = next;
        state.stateTime = 0.0f;
        ++m_stats.stateChanges;
    }
}

void PlayerController::record(
    PlayerActionKind kind, scene::EntityId player, Facing facing, std::uint32_t payload, int errorCode) {
    PlayerActionRequest request;
    request.kind = kind;
    request.player = player;
    request.facing = facing;
    request.payload = payload;
    request.errorCode = errorCode;
    m_events.push_back(request);
}

void PlayerController::reject(
    scene::EntityId player, Facing facing, std::uint32_t payload, int errorCode, bool dodgeRejection) {
    ++m_stats.rejectedActions;
    if (dodgeRejection) {
        ++m_stats.dodgeRejections;
    }
    record(PlayerActionKind::ActionRejected, player, facing, payload, errorCode);
}

void PlayerController::updateAnimation(const FacingDir& facing,
                                       const PlayerStateComp& state,
                                       const fa::physics::Velocity& velocity,
                                       bool moving) {
    m_animation.velocity = velocity.linear;
    m_animation.facing = facing.value;
    m_animation.state = state.state;
    m_animation.moving = moving;
    m_animation.speed = std::sqrt(velocity.linear.x * velocity.linear.x + velocity.linear.y * velocity.linear.y);
}

void PlayerController::update(scene::World& world, scene::EntityId player, const PlayerIntent& intent, float dt) {
    scene::Transform* transform = world.tryGet<scene::Transform>(player);
    fa::physics::Velocity* velocity = world.tryGet<fa::physics::Velocity>(player);
    FacingDir* facing = world.tryGet<FacingDir>(player);
    PlayerStateComp* state = world.tryGet<PlayerStateComp>(player);
    Stamina* stamina = world.tryGet<Stamina>(player);
    DodgeState* dodge = world.tryGet<DodgeState>(player);
    if (transform == nullptr || velocity == nullptr || facing == nullptr || state == nullptr || stamina == nullptr ||
        dodge == nullptr) {
        return;
    }
    if (!(dt > 0.0f)) {
        return;
    }

    ++m_stats.updates;
    state->stateTime += dt;
    if (state->actionLock > 0.0f) {
        state->actionLock = std::max(0.0f, state->actionLock - dt);
    }
    if (dodge->cooldown > 0.0f) {
        dodge->cooldown = std::max(0.0f, dodge->cooldown - dt);
    }

    Health* health = world.tryGet<Health>(player);
    if (health != nullptr && health->current <= 0) {
        velocity->linear = core::Vec2{0.0f, 0.0f};
        setState(*state, PlayerState::Dead);
        ++m_stats.idleSteps;
        updateAnimation(*facing, *state, *velocity, false);
        return;
    }

    const core::Vec2 move = normalizeMove(intent.move);
    const bool moving = move.x != 0.0f || move.y != 0.0f;
    const bool dodging = dodge->timeLeft > 0.0f;
    if (moving && !dodging) {
        facing->value = facingFromVector(move, facing->value);
    }

    if (intent.openInventory) {
        ++m_stats.actions;
        record(PlayerActionKind::OpenInventory, player, facing->value, intent.payload, 0);
    }
    if (intent.interact) {
        ++m_stats.actions;
        record(PlayerActionKind::Interact, player, facing->value, intent.payload, 0);
    }

    if (intent.dodge) {
        if (dodge->timeLeft > 0.0f || state->actionLock > 0.0f) {
            reject(player, facing->value, intent.payload, player_error::kBusy, true);
        } else if (dodge->cooldown > 0.0f) {
            reject(player, facing->value, intent.payload, player_error::kCooldown, true);
        } else if (stamina->current < m_tuning.dodgeStaminaCost) {
            reject(player, facing->value, intent.payload, player_error::kInsufficientStamina, true);
        } else if (!moving) {
            reject(player, facing->value, intent.payload, player_error::kNoDirection, true);
        } else {
            stamina->current -= m_tuning.dodgeStaminaCost;
            dodge->timeLeft = m_tuning.dodgeDuration;
            dodge->direction = facingFromVector(move, facing->value);
            facing->value = dodge->direction;
            ++m_stats.dodges;
            ++m_stats.actions;
            record(PlayerActionKind::Dodge, player, facing->value, intent.payload, 0);
        }
    }

    const bool dodgingNow = dodge->timeLeft > 0.0f;
    if (intent.useTool) {
        if (dodgingNow || state->actionLock > 0.0f) {
            reject(player, facing->value, intent.payload, player_error::kBusy, false);
        } else if (stamina->current < m_tuning.useToolStaminaCost) {
            reject(player, facing->value, intent.payload, player_error::kInsufficientStamina, false);
        } else {
            stamina->current -= m_tuning.useToolStaminaCost;
            setState(*state, PlayerState::UseTool);
            state->actionLock = m_tuning.actionLockDuration;
            ++m_stats.actions;
            record(PlayerActionKind::UseTool, player, facing->value, intent.payload, 0);
        }
    }
    if (intent.attack) {
        if (dodgingNow || state->actionLock > 0.0f) {
            reject(player, facing->value, intent.payload, player_error::kBusy, false);
        } else if (stamina->current < m_tuning.attackStaminaCost) {
            reject(player, facing->value, intent.payload, player_error::kInsufficientStamina, false);
        } else {
            stamina->current -= m_tuning.attackStaminaCost;
            setState(*state, PlayerState::Attack);
            state->actionLock = m_tuning.actionLockDuration;
            ++m_stats.actions;
            record(PlayerActionKind::Attack, player, facing->value, intent.payload, 0);
        }
    }

    if (dodgingNow) {
        const float speed = m_tuning.dodgeDuration > 0.0f ? m_tuning.dodgeDistance / m_tuning.dodgeDuration : 0.0f;
        velocity->linear = directionVector(dodge->direction) * speed;
        dodge->timeLeft = std::max(0.0f, dodge->timeLeft - dt);
        if (dodge->timeLeft <= 0.0f) {
            dodge->cooldown = m_tuning.dodgeCooldown;
        }
        setState(*state, PlayerState::Move);
        updateAnimation(*facing, *state, *velocity, true);
        return;
    }

    if (state->actionLock > 0.0f) {
        velocity->linear = approachVector(velocity->linear, core::Vec2{0.0f, 0.0f}, m_tuning.deceleration * dt);
        // Do not regenerate during the action lock: otherwise a 3-point tool cost
        // would be refunded by 0.2 s of regen before the lock ends.
        updateAnimation(*facing, *state, *velocity, false);
        return;
    }

    const bool wantsRun = intent.run && moving;
    const bool running = wantsRun && stamina->current > 0.0f;
    const float targetSpeed = running ? m_tuning.runSpeed : m_tuning.walkSpeed;
    const core::Vec2 desired = moving ? move * targetSpeed : core::Vec2{0.0f, 0.0f};
    const float rate = moving ? m_tuning.acceleration : m_tuning.deceleration;
    velocity->linear = approachVector(velocity->linear, desired, rate * dt);

    if (moving) {
        ++m_stats.moveSteps;
    } else {
        ++m_stats.idleSteps;
    }
    setState(*state, moving ? PlayerState::Move : PlayerState::Idle);
    if (running) {
        stamina->current = std::max(0.0f, stamina->current - m_tuning.runStaminaPerSecond * dt);
    } else if (!wantsRun) {
        // Holding run at zero stamina keeps the player walking rather than
        // stuttering between run and regen.
        stamina->current = std::min(stamina->max, stamina->current + m_tuning.staminaRegenPerSecond * dt);
    }
    updateAnimation(*facing, *state, *velocity, moving);
}

bool PlayerController::isDodging(const scene::World& world, scene::EntityId player) noexcept {
    const DodgeState* dodge = world.tryGet<DodgeState>(player);
    return dodge != nullptr && dodge->timeLeft > 0.0f;
}

bool PlayerController::isInvulnerable(const scene::World& world,
                                      scene::EntityId player,
                                      const PlayerTuning& tuning) noexcept {
    const DodgeState* dodge = world.tryGet<DodgeState>(player);
    if (dodge == nullptr || dodge->timeLeft <= 0.0f) {
        return false;
    }
    const float elapsed = tuning.dodgeDuration - dodge->timeLeft;
    return elapsed >= tuning.invulnerableStart && elapsed <= tuning.invulnerableEnd;
}

}  // namespace fa::player
