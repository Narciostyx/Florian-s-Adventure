#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "fa/player/PlayerTypes.hpp"

namespace fa::scene {
class World;
}

namespace fa::physics {
struct Velocity;
}

namespace fa::player {

/// \file
/// Moves one player entity from intent to velocity, facing, state and action requests.
///
/// The controller never writes `Transform::position`: it only writes
/// `physics::Velocity`, so `PhysicsSystem` remains the single position writer (M13).
/// It is a plain object rather than an `ISystem` so tests can drive fixed steps
/// without an `IInput` implementation; `PlayerSystem` is the input adapter.

/// \brief Applies intent to a player entity.
class PlayerController {
public:
    explicit PlayerController(PlayerTuning tuning = {});

    void setTuning(const PlayerTuning& tuning) noexcept { m_tuning = tuning; }
    [[nodiscard]] const PlayerTuning& tuning() const noexcept { return m_tuning; }

    /// \brief Adds the components the controller needs, keeping existing values.
    ///
    /// Adds `PlayerTag`, `FacingDir`, `PlayerStateComp`, `Stamina` and `DodgeState`.
    /// The entity must already have `Transform` and `physics::Velocity`; those are
    /// owned by the scene, not by the controller.
    static void
    attach(scene::World& world, scene::EntityId player, Stamina stamina = {}, Facing facing = Facing::South);

    /// \brief Advances one fixed step. `dt` must be positive.
    ///
    /// Action booleans in `intent` are edges for this step. UseTool and Attack lock
    /// movement for `PlayerTuning::actionLockDuration`; Interact does not.
    void update(scene::World& world, scene::EntityId player, const PlayerIntent& intent, float dt);

    /// \brief Whether a dodge is currently in progress.
    [[nodiscard]] static bool isDodging(const scene::World& world, scene::EntityId player) noexcept;

    /// \brief Whether the player is inside the dodge invulnerability window.
    ///
    /// M20 reads this; M16 does not implement damage.
    [[nodiscard]] static bool
    isInvulnerable(const scene::World& world, scene::EntityId player, const PlayerTuning& tuning) noexcept;

    /// \brief Requests recorded since the last `clearEvents()`.
    [[nodiscard]] std::span<const PlayerActionRequest> events() const noexcept { return m_events; }
    void clearEvents() noexcept { m_events.clear(); }

    [[nodiscard]] const PlayerStats& stats() const noexcept { return m_stats; }

    /// \brief Last velocity/facing/state, ready for M10 to consume.
    [[nodiscard]] const PlayerAnimationParams& animationParams() const noexcept { return m_animation; }

private:
    void setState(PlayerStateComp& state, PlayerState next);
    void record(PlayerActionKind kind, scene::EntityId player, Facing facing, std::uint32_t payload, int errorCode);
    void reject(scene::EntityId player, Facing facing, std::uint32_t payload, int errorCode, bool dodgeRejection);
    void updateAnimation(const FacingDir& facing,
                         const PlayerStateComp& state,
                         const fa::physics::Velocity& velocity,
                         bool moving);

    PlayerTuning m_tuning;
    std::vector<PlayerActionRequest> m_events;
    PlayerStats m_stats;
    PlayerAnimationParams m_animation;
};

}  // namespace fa::player
