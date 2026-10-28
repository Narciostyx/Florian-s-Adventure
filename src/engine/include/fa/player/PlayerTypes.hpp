#pragma once

#include <cstdint>
#include <string_view>

#include "fa/core/Math.hpp"
#include "fa/scene/EntityId.hpp"

namespace fa::player {

/// \file
/// Components and value types for the player controller.
///
/// M16 is the single place that turns intent into `Velocity`, facing and state.
/// It does not know what a tool does (M17/M19) or how damage works (M20); it only
/// records action requests for the composition root to route.

/// \brief Direction the player is facing.
///
/// Eight-way: the first four values keep the historical `North`, `East`, `South`,
/// `West` numeric order, so a composition root that only understands four-way
/// targeting can still `static_cast` those cardinals to `fa::interaction::Facing`.
/// Diagonals are appended and must be collapsed explicitly; `isDiagonal()` and
/// `cardinalFacing()` are provided for that.
enum class Facing : std::uint8_t {
    North,
    East,
    South,
    West,
    NorthEast,
    SouthEast,
    SouthWest,
    NorthWest,
    Count,
};

/// \brief True when the facing is one of the four diagonals.
[[nodiscard]] constexpr bool isDiagonal(Facing facing) noexcept {
    return facing == Facing::NorthEast || facing == Facing::SouthEast || facing == Facing::SouthWest ||
           facing == Facing::NorthWest;
}

/// \brief Collapses a diagonal to a four-way facing (vertical wins on ties).
[[nodiscard]] Facing cardinalFacing(Facing facing) noexcept;

/// \brief Coarse state used by animation, input gating and debug overlays.
enum class PlayerState : std::uint8_t {
    Idle,
    Move,
    UseTool,
    Attack,
    Hurt,
    Dead,
    Menu,
    Count,
};

/// \brief One player-originated request the composition root may route.
enum class PlayerActionKind : std::uint8_t {
    UseTool,
    Attack,
    Interact,
    OpenInventory,
    Dodge,
    /// \brief The action was refused; `PlayerActionRequest::errorCode` says why.
    ActionRejected,
    Count,
};

[[nodiscard]] std::string_view toString(Facing facing) noexcept;
[[nodiscard]] std::string_view toString(PlayerState state) noexcept;
[[nodiscard]] std::string_view toString(PlayerActionKind kind) noexcept;

/// \brief Marks the entity the player system drives.
struct PlayerTag {};

/// \brief Last significant movement direction, independent of the current velocity.
struct FacingDir {
    Facing value = Facing::South;
};

/// \brief State and timers owned by the controller.
struct PlayerStateComp {
    PlayerState state = PlayerState::Idle;
    /// \brief Seconds spent in the current state.
    float stateTime = 0.0f;
    /// \brief Seconds left in the UseTool/Attack movement lock.
    float actionLock = 0.0f;
};

/// \brief Hit points. M20 owns damage; M16 only reads zero as "dead".
struct Health {
    int current = 100;
    int max = 100;
};

/// \brief Stamina used by dodges and, later, attacks/tools.
struct Stamina {
    float current = 100.0f;
    float max = 100.0f;
};

/// \brief Currency owned by M18/M24; M16 only carries it.
struct Wallet {
    int gold = 0;
};

/// \brief Dodge timers M20 reads for invulnerability and input reads for cooldown.
struct DodgeState {
    /// \brief Seconds left in the dodge; 0 means not dodging.
    float timeLeft = 0.0f;
    /// \brief Seconds until another dodge is allowed.
    float cooldown = 0.0f;
    Facing direction = Facing::South;
};

/// \brief Tuning values from ADR-0002/ADR-0003.
struct PlayerTuning {
    /// \brief Walk speed in world pixels per second.
    float walkSpeed = 90.0f;
    /// \brief Run speed while the run action is held.
    float runSpeed = 150.0f;
    /// \brief Acceleration toward a movement target, px/s^2.
    float acceleration = 900.0f;
    /// \brief Deceleration toward zero, px/s^2.
    float deceleration = 1200.0f;
    /// \brief Total dodge distance in pixels.
    float dodgeDistance = 48.0f;
    /// \brief Dodge duration in seconds.
    float dodgeDuration = 0.25f;
    /// \brief Cooldown after a dodge ends, in seconds.
    float dodgeCooldown = 0.5f;
    /// \brief Stamina spent when a dodge starts.
    float dodgeStaminaCost = 25.0f;
    /// \brief Stamina spent when a tool request is accepted.
    float useToolStaminaCost = 3.0f;
    /// \brief Stamina spent when an attack request is accepted.
    float attackStaminaCost = 4.0f;
    /// \brief Stamina drained per second while running with movement input.
    float runStaminaPerSecond = 10.0f;
    /// \brief Stamina regenerated per second while not dodging or running.
    float staminaRegenPerSecond = 20.0f;
    /// \brief Movement lock after a tool/attack request, in seconds.
    float actionLockDuration = 0.2f;
    /// \brief Invulnerability window start, measured from the dodge start.
    float invulnerableStart = 0.05f;
    /// \brief Invulnerability window end, measured from the dodge start.
    float invulnerableEnd = 0.20f;
};

/// \brief One fixed-step input snapshot.
///
/// `move` is expected to be normalized; the controller normalizes again so a
/// scripted/demo axis and a real axis behave identically. The action booleans are
/// edges (pressed this step), except `run`, which is held.
struct PlayerIntent {
    core::Vec2 move{};
    bool run = false;
    bool useTool = false;
    bool attack = false;
    bool interact = false;
    bool dodge = false;
    bool openInventory = false;
    /// \brief Opaque item/tool payload forwarded to the request.
    std::uint32_t payload = 0;
};

/// \brief One recorded player action.
struct PlayerActionRequest {
    PlayerActionKind kind = PlayerActionKind::ActionRejected;
    scene::EntityId player{};
    Facing facing = Facing::South;
    std::uint32_t payload = 0;
    /// \brief 0 on success, otherwise a `player_error` code.
    int errorCode = 0;
};

/// \brief Aggregates for the debug overlay and the smoke summary.
struct PlayerStats {
    std::uint64_t updates = 0;
    std::uint64_t moveSteps = 0;
    std::uint64_t idleSteps = 0;
    std::uint64_t stateChanges = 0;
    std::uint64_t dodges = 0;
    std::uint64_t dodgeRejections = 0;
    std::uint64_t actions = 0;
    std::uint64_t rejectedActions = 0;
};

/// \brief What M10 will consume once animation lands.
struct PlayerAnimationParams {
    core::Vec2 velocity{};
    Facing facing = Facing::South;
    PlayerState state = PlayerState::Idle;
    float speed = 0.0f;
    bool moving = false;
};

/// \brief Action names M07 should bind for a playable character.
///
/// M07 stays gameplay-agnostic; these names live here and the composition root
/// registers/binds them. `PlayerSystem` caches the resulting ids.
inline constexpr std::string_view kActionNameRun = "Run";
inline constexpr std::string_view kActionNameDodge = "Dodge";
inline constexpr std::string_view kActionNameUseTool = "UseTool";
inline constexpr std::string_view kActionNameAttack = "Attack";
inline constexpr std::string_view kActionNameInteract = "Interact";
inline constexpr std::string_view kActionNameInventory = "Inventory";

/// \brief Eight-way facing from a movement vector, using 45-degree sectors.
///
/// Falls back to \p fallback for a zero vector. The vector is not required to be
/// normalized; only its angle matters.
[[nodiscard]] Facing facingFromVector(core::Vec2 move, Facing fallback) noexcept;

/// \brief Unit vector for a facing; y points down, matching world space.
[[nodiscard]] core::Vec2 directionVector(Facing facing) noexcept;

}  // namespace fa::player
