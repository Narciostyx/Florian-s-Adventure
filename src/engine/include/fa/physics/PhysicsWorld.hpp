#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "fa/core/Math.hpp"
#include "fa/physics/Collider.hpp"
#include "fa/physics/CollisionLayer.hpp"
#include "fa/physics/SpatialHash.hpp"
#include "fa/physics/TileCollision.hpp"
#include "fa/scene/World.hpp"

namespace fa::physics {

/// \file
/// AABB collision, axis-separated movement and spatial queries.
///
/// There is no rigid body dynamics on purpose: a top-down farming game needs "move this
/// box, do not pass through walls, slide along them", and every extra degree of freedom
/// (mass, torque, restitution) is a source of numbers that are hard to tune and harder
/// to debug with pixel art.
///
/// `PhysicsWorld` never stores a `World&`. Every call takes the world it should act on,
/// so the same instance can be reused for the next scene and no query can outlive the
/// container it was built from. It does own a broadphase index, which is rebuilt from
/// the world on `sync()`.

/// \brief Intent to move, consumed by `PhysicsWorld::step`.
///
/// Separate from `Transform` so a controller writes "I want to go this fast" while
/// physics stays the only writer of position.
struct Velocity {
    /// \brief World pixels per second.
    core::Vec2 linear{0.0f, 0.0f};
};

/// \brief What one `move()` did.
struct MoveResult {
    /// \brief The entity position after the move.
    core::Vec2 position{};
    /// \brief Whether the horizontal component was cut short by a blocker.
    bool hitX = false;
    /// \brief Whether the vertical component was cut short by a blocker.
    bool hitY = false;
    /// \brief Sweep steps the move was split into.
    ///
    /// One for anything slower than 4 px per step; more for dashes and knockback.
    /// Exposed because it is the number to look at when a fast body tunnels.
    int substeps = 0;
};

/// \brief Where a ray hit and from which side.
struct RayHit {
    /// \brief The entity that was hit, or an invalid id for a tile.
    scene::EntityId entity{};
    /// \brief World-space impact point.
    core::Vec2 point{};
    /// \brief Unit surface normal pointing back toward the ray origin.
    core::Vec2 normal{0.0f, 0.0f};
    /// \brief Distance from the origin along the ray.
    float distance = 0.0f;
    /// \brief Whether a solid tile was hit rather than an entity.
    bool hitTile = false;
};

/// \brief Which way a trigger volume changed.
enum class TriggerPhase : std::uint8_t {
    Began,
    Ended,
};

/// \brief One change in a trigger's overlap set.
struct TriggerEvent {
    /// \brief The entity whose collider is the trigger.
    scene::EntityId trigger{};
    /// \brief The other entity, either a body that entered or another trigger.
    scene::EntityId other{};
    TriggerPhase phase = TriggerPhase::Began;
};

/// \brief Something the debug overlay can draw.
///
/// Physics has no renderer: it produces geometry and the caller draws it. That keeps
/// this module linkable into a tool or a test binary with no GL context.
struct DebugShape {
    core::Rect rect{};
    core::Color color{1.0f, 1.0f, 1.0f, 1.0f};
};

/// \brief Snapshot for the debug overlay and the smoke summary.
struct PhysicsStats {
    /// \brief Entities with both a `Transform` and a `Collider`.
    std::size_t bodies = 0;
    /// \brief Of those, the ones marked `isTrigger`.
    std::size_t triggerBodies = 0;
    /// \brief Broadphase cells in use.
    std::size_t cells = 0;
    /// \brief Worst-case broadphase bucket, i.e. how much a query can over-fetch.
    std::size_t maxCellOccupancy = 0;
    /// \brief Fixed steps run.
    std::uint64_t steps = 0;
    /// \brief `move()` calls that actually had a delta.
    std::uint64_t moves = 0;
    /// \brief Moves cut short on at least one axis.
    std::uint64_t blockedMoves = 0;
    /// \brief `overlap` / `overlapCircle` calls.
    std::uint64_t overlapQueries = 0;
    /// \brief `raycast` calls.
    std::uint64_t rayQueries = 0;
    /// \brief Trigger pairs that started overlapping, accumulated.
    std::uint64_t triggerBegins = 0;
    /// \brief Trigger pairs that stopped overlapping, accumulated.
    std::uint64_t triggerEnds = 0;
    /// \brief Events produced by the most recent step.
    std::size_t triggerEvents = 0;
};

namespace detail {

/// \brief One candidate returned by the broadphase, resolved back to world space.
///
/// Internal: the lifetime of a candidate ends with the call that produced it, and it
/// carries a copy of the box so the caller never holds a component pointer across a
/// world mutation.
struct Candidate {
    scene::EntityId entity{};
    core::Rect rect{};
    CollisionMask layer = 0u;
};

/// \brief A trigger overlap, ordered so trigger events are deterministic.
struct TriggerPair {
    scene::EntityId trigger{};
    scene::EntityId other{};

    friend bool operator==(const TriggerPair& lhs, const TriggerPair& rhs) noexcept {
        return lhs.trigger == rhs.trigger && lhs.other == rhs.other;
    }

    friend bool operator<(const TriggerPair& lhs, const TriggerPair& rhs) noexcept {
        if (lhs.trigger != rhs.trigger) {
            return lhs.trigger < rhs.trigger;
        }
        return lhs.other < rhs.other;
    }
};

}  // namespace detail

/// \brief Owns the broadphase, the tile reference and the solver.
///
/// Not copyable and not movable: it holds the index and the per-step trigger state, and
/// a copy would silently duplicate the previous overlap set.
class PhysicsWorld {
public:
    PhysicsWorld() = default;
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;
    PhysicsWorld(PhysicsWorld&&) = delete;
    PhysicsWorld& operator=(PhysicsWorld&&) = delete;
    ~PhysicsWorld() = default;

    /// \brief Static geometry to collide with. Null means "no tile collision".
    void setTiles(const ISolidTiles* tiles) noexcept { m_tiles = tiles; }
    [[nodiscard]] const ISolidTiles* tiles() const noexcept { return m_tiles; }

    /// \brief Broadphase cell size; changing it rebuilds the index.
    void setCellSize(float cellSize);

    /// \brief Rebuilds the broadphase from every `Transform` + `Collider` pair.
    ///
    /// `step()` and `move()` do this themselves. Call it directly when entities were
    /// created or destroyed since the last step and a query must see them now.
    void sync(const scene::World& world);

    /// \brief Moves one entity by `delta`, sliding along blockers.
    ///
    /// The delta is split into sweeps of at most 4 px so a dash cannot cross a wall
    /// between two samples. Only `Transform::position` is written; nothing else in the
    /// world changes. Entities without a `Collider`, and triggers, move unobstructed.
    [[nodiscard]] MoveResult move(scene::World& world, scene::EntityId id, core::Vec2 delta);

    /// \brief Integrates every `Velocity` body by `dt`, then refreshes trigger events.
    ///
    /// Bodies are moved against the broadphase snapshot taken at the start of the step,
    /// so two bodies moving into the same empty cell in one step may overlap until the
    /// next one. Resolving body-versus-body ordering is a follow-up; movement against
    /// static geometry, which is what a farming game actually needs, is exact.
    void step(scene::World& world, float dt);

    /// \brief Ids whose collider overlaps `area` and matches `mask`, sorted.
    [[nodiscard]] std::vector<scene::EntityId>
    overlap(const scene::World& world, const core::Rect& area, CollisionMask mask) const;

    /// \brief Ids whose collider overlaps the circle, sorted.
    ///
    /// Uses an exact circle-versus-box test after the broadphase pass, so a corner of a
    /// box just outside the radius is correctly excluded.
    [[nodiscard]] std::vector<scene::EntityId>
    overlapCircle(const scene::World& world, core::Vec2 center, float radius, CollisionMask mask) const;

    /// \brief Nearest collider or solid tile along a ray.
    ///
    /// `direction` need not be normalized; `maxDistance` is in world pixels. Tiles are
    /// walked with a grid traversal rather than sampled, so a thin wall cannot be
    /// stepped over by a long ray.
    [[nodiscard]] std::optional<RayHit> raycast(const scene::World& world,
                                                core::Vec2 origin,
                                                core::Vec2 direction,
                                                float maxDistance,
                                                CollisionMask mask) const;

    /// \brief Overlap changes produced by the most recent `step`.
    [[nodiscard]] std::span<const TriggerEvent> triggerEvents() const noexcept { return m_triggerEvents; }

    /// \brief Appends one shape per collider, plus one per solid tile when the provider
    /// reports a finite extent.
    void debugDraw(const scene::World& world, std::vector<DebugShape>& out) const;

    [[nodiscard]] const PhysicsStats& stats() const noexcept { return m_stats; }

    /// \brief Drops the index, the trigger history and every counter.
    void clear() noexcept;

private:
    /// \brief Shared implementation of `move`, optionally skipping the re-index.
    [[nodiscard]] MoveResult moveInternal(scene::World& world, scene::EntityId id, core::Vec2 delta, bool resync);

    /// \brief Appends every solid entity box and solid tile overlapping `area`.
    void gatherBlockers(const scene::World& world,
                        const Collider& self,
                        scene::EntityId ignore,
                        const core::Rect& area,
                        std::vector<detail::Candidate>& out) const;

    void updateTriggers(const scene::World& world);
    [[nodiscard]] std::optional<RayHit> raycastTiles(core::Vec2 origin, core::Vec2 direction, float maxDistance) const;

    SpatialHash m_index;
    const ISolidTiles* m_tiles = nullptr;
    std::vector<TriggerEvent> m_triggerEvents;
    std::vector<detail::TriggerPair> m_activeTriggers;
    /// Counters are bumped from `const` queries, hence mutable. Everything else in the
    /// struct is informational.
    mutable PhysicsStats m_stats;
};

}  // namespace fa::physics
