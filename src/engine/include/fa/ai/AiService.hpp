#pragma once

#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

#include "fa/ai/AiTypes.hpp"
#include "fa/combat/CombatTypes.hpp"
#include "fa/core/Math.hpp"
#include "fa/tilemap/TileId.hpp"

namespace fa::combat {
class ICombatService;
}

namespace fa::physics {
class PhysicsWorld;
}

namespace fa::scene {
class World;
}

namespace fa::tilemap {
class Tilemap;
}

namespace fa::ai {

/// \file
/// Perception, A* pathfinding and the state machine for M21.
///
/// The service is not a `scene::ISystem`: the composition root calls `update()` at
/// the fixed step, in whatever order it wants relative to physics/combat. Movement
/// goes through `PhysicsWorld::move`; damage is always spawned as a combat attack,
/// never applied directly. M21 does not link M16 or M20 types beyond the combat
/// interface and component reads.

/// \brief Enemy/NPC decision and locomotion service.
class AiService {
public:
    /// \brief \param map Borrowed pathfinding/LOS grid.
    /// \param physics Borrowed movement executor.
    /// \param combat Borrowed attack executor; null disables attacks.
    AiService(const tilemap::Tilemap& map, physics::PhysicsWorld& physics, combat::ICombatService* combat = nullptr);

    AiService(const AiService&) = delete;
    AiService& operator=(const AiService&) = delete;
    AiService(AiService&&) = delete;
    AiService& operator=(AiService&&) = delete;

    void setCombatService(combat::ICombatService* combat) noexcept { m_combat = combat; }

    /// \brief Intercepts an agent's attack instead of spawning it immediately.
    ///
    /// The composition root uses this to play a wind-up animation and spawn the
    /// hitbox from the animation's `hit` frame. Without a handler M21 keeps its
    /// original behaviour and calls `ICombatService::spawnAttack` directly.
    using AttackHandler = std::function<void(scene::EntityId agent, const combat::AttackHitbox& hitbox)>;
    void setAttackHandler(AttackHandler handler) { m_attackHandler = std::move(handler); }

    /// \brief Advances every `AiAgent` by one fixed step.
    void update(scene::World& world, float deltaSeconds);

    /// \brief Forces \p agent to chase \p target and drops any stale path.
    void setAggro(scene::World& world, scene::EntityId agent, scene::EntityId target);

    /// \brief A* over walkable tile cells, excluding \p from and including \p to.
    ///
    /// Returns false when the goal is unreachable or the search limit is hit. An
    /// empty successful path means the two cells are the same.
    [[nodiscard]] bool
    findPath(tilemap::TileCoord from, tilemap::TileCoord to, std::vector<tilemap::TileCoord>& out) const;

    /// \brief Bresenham line of sight through solid tile cells.
    [[nodiscard]] static bool
    hasLineOfSight(const tilemap::Tilemap& map, tilemap::TileCoord from, tilemap::TileCoord to) noexcept;

    [[nodiscard]] AiStats stats() const noexcept { return m_stats; }

    /// \brief Remaining waypoints for \p agent, or 0 when it has no path.
    [[nodiscard]] std::size_t pathNodeCount(scene::EntityId agent) const noexcept;

    /// \brief Drops cached paths after a load; agent components keep their state.
    void clearPaths() noexcept { m_paths.clear(); }

    void clear() noexcept;

private:
    struct PathState {
        scene::EntityId agent{};
        std::vector<tilemap::TileCoord> nodes;
        std::size_t index = 0;
        tilemap::TileCoord goal{};
        bool hasGoal = false;
    };

    [[nodiscard]] PathState& pathFor(scene::EntityId agent);
    void resetPath(PathState& path) noexcept;
    [[nodiscard]] core::Vec2 agentCenter(const scene::World& world, scene::EntityId agent) const;
    [[nodiscard]] tilemap::TileCoord cellAt(const scene::World& world, scene::EntityId agent) const;
    [[nodiscard]] bool ensurePath(const scene::World& world, AiAgent& agent, PathState& path, tilemap::TileCoord goal);
    void moveAlongPath(scene::World& world, scene::EntityId id, AiAgent& agent, PathState& path, float deltaSeconds);
    void perceive(scene::World& world, scene::EntityId id, AiAgent& agent, core::Vec2 center);
    void performAttack(
        scene::World& world, scene::EntityId id, const AiAgent& agent, core::Vec2 center, core::Vec2 targetCenter);
    void changeState(AiAgent& agent, AiState state);

    const tilemap::Tilemap& m_map;
    physics::PhysicsWorld& m_physics;
    combat::ICombatService* m_combat = nullptr;
    AttackHandler m_attackHandler;
    std::vector<PathState> m_paths;
    AiStats m_stats;
    std::uint64_t m_step = 0;
};

}  // namespace fa::ai
