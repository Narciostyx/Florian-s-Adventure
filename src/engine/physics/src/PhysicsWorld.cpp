#include "fa/physics/PhysicsWorld.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace fa::physics {

namespace {

/// \brief Gap below which a box counts as touching rather than penetrating.
constexpr float kContactTolerance = 1.0e-3f;

/// \brief Longest distance one sweep covers.
///
/// A delta of 192 px/s at 60 Hz is 3.2 px, so ordinary movement needs one sweep. Dashes
/// and knockback get split instead of being allowed to hop a 16 px wall.
constexpr float kMaxSubstepDistance = 4.0f;

/// \brief Stand-in for "this blocker does not limit the move".
constexpr float kNoLimit = 1.0e30f;

/// \brief Guard for the grid traversal in `raycastTiles`.
constexpr int kMaxRaySteps = 1024;

/// \brief Treats anything below this as a zero direction component.
constexpr float kDirectionEpsilon = 1.0e-6f;

[[nodiscard]] bool rangesOverlap(float minA, float maxA, float minB, float maxB) noexcept {
    return minA < maxB && minB < maxA;
}

/// \brief How far `mover` may travel along x before its leading edge reaches `blocker`.
///
/// `kNoLimit` means the boxes do not share a row, or they already overlap by more than
/// the contact tolerance. Ignoring a real overlap is deliberate: a body that starts
/// stuck inside a wall must be able to walk out of it rather than being pinned forever.
[[nodiscard]] float limitAlongX(const core::Rect& mover, const core::Rect& blocker, float delta) noexcept {
    if (!rangesOverlap(mover.y, core::bottom(mover), blocker.y, core::bottom(blocker))) {
        return kNoLimit;
    }
    const float gap = delta > 0.0f ? blocker.x - core::right(mover) : mover.x - core::right(blocker);
    if (gap < -kContactTolerance) {
        return kNoLimit;
    }
    return gap > 0.0f ? gap : 0.0f;
}

/// \brief How far `mover` may travel along y before its leading edge reaches `blocker`.
[[nodiscard]] float limitAlongY(const core::Rect& mover, const core::Rect& blocker, float delta) noexcept {
    if (!rangesOverlap(mover.x, core::right(mover), blocker.x, core::right(blocker))) {
        return kNoLimit;
    }
    const float gap = delta > 0.0f ? blocker.y - core::bottom(mover) : mover.y - core::bottom(blocker);
    if (gap < -kContactTolerance) {
        return kNoLimit;
    }
    return gap > 0.0f ? gap : 0.0f;
}

/// \brief Distance along `unit` at which it first enters `rect`, or -1 when it misses.
///
/// A ray starting inside the box reports distance 0, which is the honest answer for an
/// attack that already overlaps its target.
[[nodiscard]] float rayBox(core::Vec2 origin, core::Vec2 unit, const core::Rect& rect, float maxDistance) noexcept {
    float near = 0.0f;
    float far = maxDistance;

    if (std::fabs(unit.x) < kDirectionEpsilon) {
        if (origin.x < rect.x || origin.x > core::right(rect)) {
            return -1.0f;
        }
    } else {
        const float inverse = 1.0f / unit.x;
        float entry = (rect.x - origin.x) * inverse;
        float exit = (core::right(rect) - origin.x) * inverse;
        if (entry > exit) {
            std::swap(entry, exit);
        }
        near = std::max(near, entry);
        far = std::min(far, exit);
        if (near > far) {
            return -1.0f;
        }
    }

    if (std::fabs(unit.y) < kDirectionEpsilon) {
        if (origin.y < rect.y || origin.y > core::bottom(rect)) {
            return -1.0f;
        }
    } else {
        const float inverse = 1.0f / unit.y;
        float entry = (rect.y - origin.y) * inverse;
        float exit = (core::bottom(rect) - origin.y) * inverse;
        if (entry > exit) {
            std::swap(entry, exit);
        }
        near = std::max(near, entry);
        far = std::min(far, exit);
        if (near > far) {
            return -1.0f;
        }
    }

    return near;
}

/// \brief Outward normal of whichever box face `point` sits on.
[[nodiscard]] core::Vec2 surfaceNormal(core::Vec2 point, const core::Rect& rect) noexcept {
    const float tolerance = 1.0e-3f;
    if (std::fabs(point.x - rect.x) <= tolerance) {
        return core::Vec2{-1.0f, 0.0f};
    }
    if (std::fabs(point.x - core::right(rect)) <= tolerance) {
        return core::Vec2{1.0f, 0.0f};
    }
    if (std::fabs(point.y - rect.y) <= tolerance) {
        return core::Vec2{0.0f, -1.0f};
    }
    if (std::fabs(point.y - core::bottom(rect)) <= tolerance) {
        return core::Vec2{0.0f, 1.0f};
    }
    return core::Vec2{0.0f, 0.0f};
}

}  // namespace

void PhysicsWorld::setCellSize(float cellSize) {
    m_index.setCellSize(cellSize);
}

void PhysicsWorld::sync(const scene::World& world) {
    m_index.clear();
    m_stats.bodies = 0;
    m_stats.triggerBodies = 0;
    world.each<scene::Transform, Collider>(
        [this](scene::EntityId id, const scene::Transform& transform, const Collider& collider) {
            ++m_stats.bodies;
            if (collider.isTrigger) {
                ++m_stats.triggerBodies;
            }
            m_index.insert(id, colliderRect(transform, collider));
        });
    m_stats.cells = m_index.cellCount();
    m_stats.maxCellOccupancy = m_index.maxCellOccupancy();
}

MoveResult PhysicsWorld::move(scene::World& world, scene::EntityId id, core::Vec2 delta) {
    return moveInternal(world, id, delta, true);
}

MoveResult PhysicsWorld::moveInternal(scene::World& world, scene::EntityId id, core::Vec2 delta, bool resync) {
    MoveResult result;
    scene::Transform* transform = world.tryGet<scene::Transform>(id);
    if (transform == nullptr) {
        return result;
    }
    result.position = transform->position;

    const Collider* collider = world.tryGet<Collider>(id);
    const bool collides = collider != nullptr && !collider->isTrigger;
    if (!collides || (delta.x == 0.0f && delta.y == 0.0f)) {
        transform->position += delta;
        result.position = transform->position;
        return result;
    }

    if (resync) {
        sync(world);
    }
    ++m_stats.moves;

    const float longest = std::max(std::fabs(delta.x), std::fabs(delta.y));
    int substeps = static_cast<int>(std::ceil(longest / kMaxSubstepDistance));
    if (substeps < 1) {
        substeps = 1;
    }
    result.substeps = substeps;
    const core::Vec2 step{delta.x / static_cast<float>(substeps), delta.y / static_cast<float>(substeps)};

    std::vector<detail::Candidate> blockers;
    for (int index = 0; index < substeps; ++index) {
        if (step.x != 0.0f) {
            const core::Rect mover = colliderRect(*transform, *collider);
            core::Rect band = mover;
            if (step.x > 0.0f) {
                band.w += step.x;
            } else {
                band.x += step.x;
                band.w -= step.x;
            }
            blockers.clear();
            gatherBlockers(world, *collider, id, band, blockers);

            float allowed = std::fabs(step.x);
            for (const detail::Candidate& blocker : blockers) {
                const float limit = limitAlongX(mover, blocker.rect, step.x);
                if (limit < allowed) {
                    allowed = limit;
                }
            }
            transform->position.x += step.x > 0.0f ? allowed : -allowed;
            if (allowed < std::fabs(step.x) - kContactTolerance) {
                result.hitX = true;
            }
        }

        if (step.y != 0.0f) {
            const core::Rect mover = colliderRect(*transform, *collider);
            core::Rect band = mover;
            if (step.y > 0.0f) {
                band.h += step.y;
            } else {
                band.y += step.y;
                band.h -= step.y;
            }
            blockers.clear();
            gatherBlockers(world, *collider, id, band, blockers);

            float allowed = std::fabs(step.y);
            for (const detail::Candidate& blocker : blockers) {
                const float limit = limitAlongY(mover, blocker.rect, step.y);
                if (limit < allowed) {
                    allowed = limit;
                }
            }
            transform->position.y += step.y > 0.0f ? allowed : -allowed;
            if (allowed < std::fabs(step.y) - kContactTolerance) {
                result.hitY = true;
            }
        }
    }

    if (result.hitX || result.hitY) {
        ++m_stats.blockedMoves;
    }
    result.position = transform->position;
    return result;
}

void PhysicsWorld::step(scene::World& world, float dt) {
    ++m_stats.steps;
    m_triggerEvents.clear();
    sync(world);

    if (dt > 0.0f) {
        // Collect first, then move: `gatherBlockers` walks the broadphase while a move
        // mutates a transform, and re-entering `World::each` from inside its own callback
        // would make the component references the callback holds hard to reason about.
        std::vector<scene::EntityId> movers;
        world.each<scene::Transform, Velocity>(
            [&movers](scene::EntityId id, const scene::Transform&, const Velocity& velocity) {
                if (velocity.linear.x != 0.0f || velocity.linear.y != 0.0f) {
                    movers.push_back(id);
                }
            });
        for (const scene::EntityId id : movers) {
            const Velocity* velocity = world.tryGet<Velocity>(id);
            if (velocity == nullptr) {
                continue;
            }
            const core::Vec2 delta{velocity->linear.x * dt, velocity->linear.y * dt};
            static_cast<void>(moveInternal(world, id, delta, false));
        }
    }

    // Trigger overlap must be judged on the positions the step produced, so the index is
    // rebuilt once more before comparing volumes.
    sync(world);
    updateTriggers(world);
    m_stats.triggerEvents = m_triggerEvents.size();
}

std::vector<scene::EntityId>
PhysicsWorld::overlap(const scene::World& world, const core::Rect& area, CollisionMask mask) const {
    ++m_stats.overlapQueries;
    std::vector<scene::EntityId> result;
    for (const scene::EntityId id : m_index.queryUnique(area)) {
        const scene::Transform* transform = world.tryGet<scene::Transform>(id);
        const Collider* collider = world.tryGet<Collider>(id);
        if (transform == nullptr || collider == nullptr) {
            continue;
        }
        if (!matches(maskOf(collider->layer), mask)) {
            continue;
        }
        if (!core::rectOverlaps(area, colliderRect(*transform, *collider))) {
            continue;
        }
        result.push_back(id);
    }
    return result;
}

std::vector<scene::EntityId>
PhysicsWorld::overlapCircle(const scene::World& world, core::Vec2 center, float radius, CollisionMask mask) const {
    ++m_stats.overlapQueries;
    std::vector<scene::EntityId> result;
    if (radius <= 0.0f) {
        return result;
    }
    const core::Rect area = core::rectFromCenter(center, radius * 2.0f, radius * 2.0f);
    const float radiusSquared = radius * radius;
    for (const scene::EntityId id : m_index.queryUnique(area)) {
        const scene::Transform* transform = world.tryGet<scene::Transform>(id);
        const Collider* collider = world.tryGet<Collider>(id);
        if (transform == nullptr || collider == nullptr) {
            continue;
        }
        if (!matches(maskOf(collider->layer), mask)) {
            continue;
        }
        const core::Rect rect = colliderRect(*transform, *collider);
        const core::Vec2 closest{core::clampf(center.x, rect.x, core::right(rect)),
                                 core::clampf(center.y, rect.y, core::bottom(rect))};
        const core::Vec2 offset{center.x - closest.x, center.y - closest.y};
        if (offset.x * offset.x + offset.y * offset.y <= radiusSquared) {
            result.push_back(id);
        }
    }
    return result;
}

std::optional<RayHit> PhysicsWorld::raycast(
    const scene::World& world, core::Vec2 origin, core::Vec2 direction, float maxDistance, CollisionMask mask) const {
    ++m_stats.rayQueries;
    if (maxDistance <= 0.0f) {
        return std::nullopt;
    }
    const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    if (length <= kDirectionEpsilon) {
        return std::nullopt;
    }
    const core::Vec2 unit{direction.x / length, direction.y / length};

    std::optional<RayHit> best = raycastTiles(origin, unit, maxDistance);

    const core::Vec2 end{origin.x + unit.x * maxDistance, origin.y + unit.y * maxDistance};
    const core::Rect band{
        std::min(origin.x, end.x), std::min(origin.y, end.y), std::fabs(end.x - origin.x), std::fabs(end.y - origin.y)};
    for (const scene::EntityId id : m_index.queryUnique(band)) {
        const scene::Transform* transform = world.tryGet<scene::Transform>(id);
        const Collider* collider = world.tryGet<Collider>(id);
        if (transform == nullptr || collider == nullptr) {
            continue;
        }
        if (!matches(maskOf(collider->layer), mask)) {
            continue;
        }
        const core::Rect rect = colliderRect(*transform, *collider);
        const float distance = rayBox(origin, unit, rect, maxDistance);
        if (distance < 0.0f) {
            continue;
        }
        if (best.has_value() && distance >= best->distance) {
            continue;
        }
        RayHit hit;
        hit.entity = id;
        hit.distance = distance;
        hit.point = core::Vec2{origin.x + unit.x * distance, origin.y + unit.y * distance};
        hit.normal = surfaceNormal(hit.point, rect);
        hit.hitTile = false;
        best = hit;
    }
    return best;
}

void PhysicsWorld::debugDraw(const scene::World& world, std::vector<DebugShape>& out) const {
    world.each<scene::Transform, Collider>(
        [&out](scene::EntityId, const scene::Transform& transform, const Collider& collider) {
            out.push_back(DebugShape{colliderRect(transform, collider),
                                     collider.isTrigger ? core::Color{1.0f, 0.85f, 0.2f, 0.8f}
                                                        : core::Color{0.2f, 0.9f, 0.9f, 0.8f}});
        });

    if (m_tiles == nullptr) {
        return;
    }
    const int columns = m_tiles->columns();
    const int rows = m_tiles->rows();
    const float tileSize = m_tiles->tileSize();
    if (columns <= 0 || rows <= 0 || tileSize <= 0.0f) {
        return;
    }
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            if (!m_tiles->isSolid(column, row)) {
                continue;
            }
            out.push_back(DebugShape{
                core::makeRect(
                    static_cast<float>(column) * tileSize, static_cast<float>(row) * tileSize, tileSize, tileSize),
                core::Color{0.9f, 0.3f, 0.3f, 0.35f}});
        }
    }
}

void PhysicsWorld::clear() noexcept {
    m_index.clear();
    m_triggerEvents.clear();
    m_activeTriggers.clear();
    m_stats = PhysicsStats{};
}

void PhysicsWorld::gatherBlockers(const scene::World& world,
                                  const Collider& self,
                                  scene::EntityId ignore,
                                  const core::Rect& area,
                                  std::vector<detail::Candidate>& out) const {
    for (const scene::EntityId other : m_index.queryUnique(area)) {
        if (other == ignore) {
            continue;
        }
        const scene::Transform* transform = world.tryGet<scene::Transform>(other);
        const Collider* collider = world.tryGet<Collider>(other);
        if (transform == nullptr || collider == nullptr || collider->isTrigger) {
            continue;
        }
        if (!layersInteract(self, *collider)) {
            continue;
        }
        out.push_back(detail::Candidate{other, colliderRect(*transform, *collider), maskOf(collider->layer)});
    }

    // Tiles are not entities, so they are appended after the broadphase pass. A collider
    // whose mask omits World is the documented way to make something fly over terrain.
    if (m_tiles == nullptr || !matches(maskOf(CollisionLayer::World), self.mask)) {
        return;
    }
    const float tileSize = m_tiles->tileSize();
    if (tileSize <= 0.0f) {
        return;
    }
    const float inverse = 1.0f / tileSize;
    const int minColumn = static_cast<int>(std::floor(area.x * inverse));
    const int minRow = static_cast<int>(std::floor(area.y * inverse));
    const int maxColumnCandidate = static_cast<int>(std::ceil(core::right(area) * inverse)) - 1;
    const int maxRowCandidate = static_cast<int>(std::ceil(core::bottom(area) * inverse)) - 1;
    const int maxColumn = maxColumnCandidate < minColumn ? minColumn : maxColumnCandidate;
    const int maxRow = maxRowCandidate < minRow ? minRow : maxRowCandidate;
    for (int row = minRow; row <= maxRow; ++row) {
        for (int column = minColumn; column <= maxColumn; ++column) {
            if (!m_tiles->isSolid(column, row)) {
                continue;
            }
            out.push_back(detail::Candidate{
                scene::EntityId{},
                core::makeRect(
                    static_cast<float>(column) * tileSize, static_cast<float>(row) * tileSize, tileSize, tileSize),
                maskOf(CollisionLayer::World)});
        }
    }
}

void PhysicsWorld::updateTriggers(const scene::World& world) {
    std::vector<scene::EntityId> triggers;
    world.each<scene::Transform, Collider>(
        [&triggers](scene::EntityId id, const scene::Transform&, const Collider& collider) {
            if (collider.isTrigger) {
                triggers.push_back(id);
            }
        });

    std::vector<detail::TriggerPair> current;
    for (const scene::EntityId triggerId : triggers) {
        const scene::Transform* triggerTransform = world.tryGet<scene::Transform>(triggerId);
        const Collider* triggerCollider = world.tryGet<Collider>(triggerId);
        if (triggerTransform == nullptr || triggerCollider == nullptr) {
            continue;
        }
        const core::Rect triggerRect = colliderRect(*triggerTransform, *triggerCollider);
        for (const scene::EntityId other : m_index.queryUnique(triggerRect)) {
            if (other == triggerId) {
                continue;
            }
            const scene::Transform* otherTransform = world.tryGet<scene::Transform>(other);
            const Collider* otherCollider = world.tryGet<Collider>(other);
            if (otherTransform == nullptr || otherCollider == nullptr) {
                continue;
            }
            if (!layersInteract(*triggerCollider, *otherCollider)) {
                continue;
            }
            if (!core::rectOverlaps(triggerRect, colliderRect(*otherTransform, *otherCollider))) {
                continue;
            }
            current.push_back(detail::TriggerPair{triggerId, other});
        }
    }
    std::sort(current.begin(), current.end());
    current.erase(std::unique(current.begin(), current.end()), current.end());

    std::size_t previousIndex = 0;
    std::size_t currentIndex = 0;
    while (previousIndex < m_activeTriggers.size() && currentIndex < current.size()) {
        const detail::TriggerPair& previous = m_activeTriggers[previousIndex];
        const detail::TriggerPair& next = current[currentIndex];
        if (previous == next) {
            ++previousIndex;
            ++currentIndex;
            continue;
        }
        if (next < previous) {
            m_triggerEvents.push_back(TriggerEvent{next.trigger, next.other, TriggerPhase::Began});
            ++m_stats.triggerBegins;
            ++currentIndex;
        } else {
            m_triggerEvents.push_back(TriggerEvent{previous.trigger, previous.other, TriggerPhase::Ended});
            ++m_stats.triggerEnds;
            ++previousIndex;
        }
    }
    for (; currentIndex < current.size(); ++currentIndex) {
        const detail::TriggerPair& pair = current[currentIndex];
        m_triggerEvents.push_back(TriggerEvent{pair.trigger, pair.other, TriggerPhase::Began});
        ++m_stats.triggerBegins;
    }
    for (; previousIndex < m_activeTriggers.size(); ++previousIndex) {
        const detail::TriggerPair& pair = m_activeTriggers[previousIndex];
        m_triggerEvents.push_back(TriggerEvent{pair.trigger, pair.other, TriggerPhase::Ended});
        ++m_stats.triggerEnds;
    }

    m_activeTriggers = std::move(current);
}

std::optional<RayHit> PhysicsWorld::raycastTiles(core::Vec2 origin, core::Vec2 direction, float maxDistance) const {
    if (m_tiles == nullptr) {
        return std::nullopt;
    }
    const float tileSize = m_tiles->tileSize();
    if (tileSize <= 0.0f) {
        return std::nullopt;
    }
    const float inverse = 1.0f / tileSize;

    int column = static_cast<int>(std::floor(origin.x * inverse));
    int row = static_cast<int>(std::floor(origin.y * inverse));
    const int stepX = direction.x > 0.0f ? 1 : (direction.x < 0.0f ? -1 : 0);
    const int stepY = direction.y > 0.0f ? 1 : (direction.y < 0.0f ? -1 : 0);

    const float deltaX = stepX != 0 ? tileSize / std::fabs(direction.x) : kNoLimit;
    const float deltaY = stepY != 0 ? tileSize / std::fabs(direction.y) : kNoLimit;

    const float boundaryX =
        stepX > 0 ? static_cast<float>(column + 1) * tileSize : static_cast<float>(column) * tileSize;
    const float boundaryY = stepY > 0 ? static_cast<float>(row + 1) * tileSize : static_cast<float>(row) * tileSize;

    float nextX = stepX != 0 ? (boundaryX - origin.x) / direction.x : kNoLimit;
    float nextY = stepY != 0 ? (boundaryY - origin.y) / direction.y : kNoLimit;
    if (nextX < 0.0f) {
        nextX = 0.0f;
    }
    if (nextY < 0.0f) {
        nextY = 0.0f;
    }

    float distance = 0.0f;
    core::Vec2 normal{0.0f, 0.0f};
    for (int guard = 0; guard < kMaxRaySteps; ++guard) {
        if (m_tiles->isSolid(column, row)) {
            RayHit hit;
            hit.distance = distance;
            hit.point = core::Vec2{origin.x + direction.x * distance, origin.y + direction.y * distance};
            hit.normal = normal;
            hit.hitTile = true;
            return hit;
        }
        if (nextX < nextY) {
            distance = nextX;
            nextX += deltaX;
            column += stepX;
            normal = core::Vec2{stepX > 0 ? -1.0f : 1.0f, 0.0f};
        } else {
            distance = nextY;
            nextY += deltaY;
            row += stepY;
            normal = core::Vec2{0.0f, stepY > 0 ? -1.0f : 1.0f};
        }
        if (distance > maxDistance) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

}  // namespace fa::physics
