#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "fa/core/Math.hpp"
#include "fa/physics/CollisionLayer.hpp"
#include "fa/scene/EntityId.hpp"

namespace fa::ai {

/// \file
/// Value types for M21: agent state, perception/attack tuning and counters.
///
/// The AI component stays plain data (no path container in ECS); `AiService` owns
/// the per-agent path next to the component. This keeps `AiAgent` cheap to copy
/// and avoids giving every enemy a heap allocation while it is idle.

/// \brief Coarse decision state. Numeric values are not a save contract.
enum class AiState : std::uint8_t {
    Idle = 0,
    Patrol,
    Chase,
    Attack,
    Flee,
    Return,
    Count,
};

/// \brief Stable name for logs, the debug overlay and tests.
[[nodiscard]] std::string_view toString(AiState state) noexcept;

/// \brief Marks an entity the AI may treat as a target.
///
/// M21 deliberately does not include M16: the composition root adds this tag to
/// the player, a decoy or any other actor that should be perceivable.
struct AiTargetTag {};

/// \brief Per-agent tuning, normally filled from `EnemyDef` by the composition root.
struct BehaviorProfile {
    float sightRadius = 120.0f;
    float attackRange = 20.0f;
    float moveSpeed = 60.0f;
    int attackDamage = 5;
    float attackCooldown = 1.0f;
    /// \brief Health ratio at or below which the agent flees; 0 disables fleeing.
    float fleeHealthRatio = 0.2f;
    /// \brief Seconds without a visible target before returning home.
    float loseTargetSeconds = 3.0f;
    /// \brief Seconds between path recomputations while chasing.
    float repathSeconds = 0.5f;
    /// \brief Fixed steps between perception sweeps.
    int perceptionIntervalSteps = 6;
    /// \brief Distance at which a waypoint counts as reached.
    float arriveRadius = 4.0f;
    /// \brief Radius of the patrol target around the home point.
    float patrolRadius = 48.0f;
    bool patrol = false;
    /// \brief Layers an attack may hit. Defaults to the player layer.
    physics::CollisionMask attackMask = physics::maskOf(physics::CollisionLayer::Player);
};

/// \brief Plain-data AI component.
struct AiAgent {
    AiState state = AiState::Idle;
    core::Vec2 home{};
    float stateTime = 0.0f;
    float attackCooldown = 0.0f;
    float loseTimer = 0.0f;
    float repathTimer = 0.0f;
    scene::EntityId target{};
    bool targetVisible = false;
    BehaviorProfile profile;
};

/// \brief Save-facing subset of `AiAgent`.
///
/// Entity ids are scene-local, so the target is not part of the snapshot; the
/// composition root stores a stable role and resolves it after the scene loads.
struct AiAgentSnapshot {
    AiState state = AiState::Idle;
    core::Vec2 home{};
    float stateTime = 0.0f;
    float attackCooldown = 0.0f;
    float loseTimer = 0.0f;
    float repathTimer = 0.0f;
    bool targetVisible = false;
};

/// \brief Copies the persistent fields out of an agent component.
[[nodiscard]] AiAgentSnapshot captureAiAgent(const AiAgent& agent) noexcept;

/// \brief Writes a snapshot back; an unknown state falls back to `Idle`.
void applyAiAgentSnapshot(AiAgent& agent, const AiAgentSnapshot& snapshot) noexcept;

/// \brief Counters for the smoke summary and M27.
struct AiStats {
    std::size_t agents = 0;
    std::uint64_t steps = 0;
    std::uint64_t perceptions = 0;
    std::uint64_t targetsFound = 0;
    std::uint64_t pathsRequested = 0;
    std::uint64_t pathsFound = 0;
    std::uint64_t moves = 0;
    std::uint64_t attacks = 0;
    std::uint64_t stateChanges = 0;
    std::uint64_t pathNodes = 0;
};

}  // namespace fa::ai
