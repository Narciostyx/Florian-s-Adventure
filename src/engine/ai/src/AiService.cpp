#include "fa/ai/AiService.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "fa/combat/CombatService.hpp"
#include "fa/physics/Collider.hpp"
#include "fa/physics/PhysicsWorld.hpp"
#include "fa/player/PlayerTypes.hpp"
#include "fa/scene/Components.hpp"
#include "fa/scene/World.hpp"
#include "fa/tilemap/Tilemap.hpp"

namespace fa::ai {
namespace {

constexpr std::size_t kMaxPathNodes = 4096;

[[nodiscard]] std::uint64_t cellKey(tilemap::TileCoord cell) noexcept {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cell.x)) << 32) | static_cast<std::uint32_t>(cell.y);
}

[[nodiscard]] float distanceBetween(core::Vec2 lhs, core::Vec2 rhs) noexcept {
    return glm::length(rhs - lhs);
}

[[nodiscard]] tilemap::TileCoord offsetCell(tilemap::TileCoord cell, int dx, int dy) noexcept {
    return tilemap::TileCoord{cell.x + dx, cell.y + dy};
}

}  // namespace

std::string_view toString(AiState state) noexcept {
    switch (state) {
        case AiState::Idle:
            return "idle";
        case AiState::Patrol:
            return "patrol";
        case AiState::Chase:
            return "chase";
        case AiState::Attack:
            return "attack";
        case AiState::Flee:
            return "flee";
        case AiState::Return:
            return "return";
        default:
            return "?";
    }
}

AiAgentSnapshot captureAiAgent(const AiAgent& agent) noexcept {
    AiAgentSnapshot result;
    result.state = agent.state;
    result.home = agent.home;
    result.stateTime = agent.stateTime;
    result.attackCooldown = agent.attackCooldown;
    result.loseTimer = agent.loseTimer;
    result.repathTimer = agent.repathTimer;
    result.targetVisible = agent.targetVisible;
    return result;
}

void applyAiAgentSnapshot(AiAgent& agent, const AiAgentSnapshot& snapshot) noexcept {
    const auto rawState = static_cast<std::uint8_t>(snapshot.state);
    agent.state = rawState < static_cast<std::uint8_t>(AiState::Count) ? snapshot.state : AiState::Idle;
    agent.home = snapshot.home;
    agent.stateTime = snapshot.stateTime;
    agent.attackCooldown = snapshot.attackCooldown;
    agent.loseTimer = snapshot.loseTimer;
    agent.repathTimer = snapshot.repathTimer;
    agent.targetVisible = snapshot.targetVisible;
}

AiService::AiService(const tilemap::Tilemap& map, physics::PhysicsWorld& physics, combat::ICombatService* combat)
    : m_map(map), m_physics(physics), m_combat(combat) {}

void AiService::clear() noexcept {
    m_paths.clear();
    m_stats = AiStats{};
    m_step = 0;
}

std::size_t AiService::pathNodeCount(scene::EntityId agent) const noexcept {
    for (const PathState& path : m_paths) {
        if (path.agent == agent && path.hasGoal) {
            return path.nodes.size() - std::min(path.index, path.nodes.size());
        }
    }
    return 0;
}

bool AiService::hasLineOfSight(const tilemap::Tilemap& map, tilemap::TileCoord from, tilemap::TileCoord to) noexcept {
    int x = from.x;
    int y = from.y;
    const int dx = std::abs(to.x - from.x);
    const int dy = -std::abs(to.y - from.y);
    const int sx = from.x < to.x ? 1 : -1;
    const int sy = from.y < to.y ? 1 : -1;
    int error = dx + dy;

    while (true) {
        const bool isStart = x == from.x && y == from.y;
        const bool isGoal = x == to.x && y == to.y;
        if (!isStart && !isGoal && map.isSolidCell(x, y)) {
            return false;
        }
        if (isGoal) {
            return true;
        }
        const int doubled = 2 * error;
        if (doubled >= dy) {
            error += dy;
            x += sx;
        }
        if (doubled <= dx) {
            error += dx;
            y += sy;
        }
    }
}

bool AiService::findPath(tilemap::TileCoord from, tilemap::TileCoord to, std::vector<tilemap::TileCoord>& out) const {
    out.clear();
    if (!m_map.contains(from.x, from.y) || !m_map.contains(to.x, to.y)) {
        return false;
    }
    if (from == to) {
        return true;
    }
    if (!m_map.isWalkable(to.x, to.y)) {
        return false;
    }

    struct OpenNode {
        tilemap::TileCoord cell{};
        float g = 0.0f;
        float f = 0.0f;
    };

    const auto heuristic = [](tilemap::TileCoord lhs, tilemap::TileCoord rhs) {
        return static_cast<float>(std::abs(lhs.x - rhs.x) + std::abs(lhs.y - rhs.y));
    };
    const auto better = [](const OpenNode& lhs, const OpenNode& rhs) {
        if (lhs.f != rhs.f) {
            return lhs.f < rhs.f;
        }
        if (lhs.g != rhs.g) {
            return lhs.g > rhs.g;
        }
        if (lhs.cell.y != rhs.cell.y) {
            return lhs.cell.y < rhs.cell.y;
        }
        return lhs.cell.x < rhs.cell.x;
    };

    std::vector<OpenNode> open;
    open.push_back(OpenNode{from, 0.0f, heuristic(from, to)});
    std::unordered_map<std::uint64_t, float> gScore;
    std::unordered_map<std::uint64_t, tilemap::TileCoord> cameFrom;
    std::unordered_set<std::uint64_t> closed;
    gScore[cellKey(from)] = 0.0f;

    constexpr int kDx[4] = {1, -1, 0, 0};
    constexpr int kDy[4] = {0, 0, 1, -1};
    std::size_t expansions = 0;

    while (!open.empty() && expansions < kMaxPathNodes) {
        const auto best = std::min_element(open.begin(), open.end(), better);
        const OpenNode current = *best;
        open.erase(best);

        const std::uint64_t currentKey = cellKey(current.cell);
        if (closed.count(currentKey) != 0) {
            continue;
        }
        closed.insert(currentKey);
        ++expansions;

        if (current.cell == to) {
            std::vector<tilemap::TileCoord> raw;
            tilemap::TileCoord at = to;
            while (at != from) {
                raw.push_back(at);
                const auto previous = cameFrom.find(cellKey(at));
                if (previous == cameFrom.end()) {
                    return false;
                }
                at = previous->second;
            }
            std::reverse(raw.begin(), raw.end());

            tilemap::TileCoord anchor = from;
            std::size_t index = 0;
            while (index < raw.size()) {
                std::size_t farthest = index;
                for (std::size_t candidate = raw.size(); candidate-- > index;) {
                    if (hasLineOfSight(m_map, anchor, raw[candidate])) {
                        farthest = candidate;
                        break;
                    }
                }
                out.push_back(raw[farthest]);
                anchor = raw[farthest];
                index = farthest + 1;
            }
            return true;
        }

        for (int neighbour = 0; neighbour < 4; ++neighbour) {
            const tilemap::TileCoord next = offsetCell(current.cell, kDx[neighbour], kDy[neighbour]);
            if (!m_map.contains(next.x, next.y) || !m_map.isWalkable(next.x, next.y)) {
                continue;
            }
            const std::uint64_t nextKey = cellKey(next);
            if (closed.count(nextKey) != 0) {
                continue;
            }
            const float tentative = current.g + 1.0f;
            const auto known = gScore.find(nextKey);
            if (known == gScore.end() || tentative < known->second) {
                gScore[nextKey] = tentative;
                cameFrom[nextKey] = current.cell;
                open.push_back(OpenNode{next, tentative, tentative + heuristic(next, to)});
            }
        }
    }
    return false;
}

void AiService::update(scene::World& world, float deltaSeconds) {
    ++m_step;
    ++m_stats.steps;
    m_stats.agents = 0;

    world.each<AiAgent, scene::Transform>([&](scene::EntityId id, AiAgent& agent, const scene::Transform&) {
        ++m_stats.agents;
        PathState& path = pathFor(id);
        agent.stateTime += deltaSeconds;
        agent.attackCooldown = std::max(0.0f, agent.attackCooldown - deltaSeconds);
        agent.repathTimer = std::max(0.0f, agent.repathTimer - deltaSeconds);

        const player::Health* health = world.tryGet<player::Health>(id);
        if (health != nullptr && health->current <= 0) {
            resetPath(path);
            changeState(agent, AiState::Idle);
            return;
        }

        const core::Vec2 center = agentCenter(world, id);
        const int interval = std::max(1, agent.profile.perceptionIntervalSteps);
        if (m_step % static_cast<std::uint64_t>(interval) == 0) {
            perceive(world, id, agent, center);
        }

        if (agent.target.isValid()) {
            const scene::Transform* targetTransform = world.tryGet<scene::Transform>(agent.target);
            const player::Health* targetHealth = world.tryGet<player::Health>(agent.target);
            if (targetTransform == nullptr || (targetHealth != nullptr && targetHealth->current <= 0)) {
                agent.target = scene::EntityId{};
                agent.targetVisible = false;
            }
        }

        const bool shouldFlee = health != nullptr && health->max > 0 &&
                                health->current <= health->max * agent.profile.fleeHealthRatio &&
                                agent.target.isValid();
        if (shouldFlee && agent.state != AiState::Flee) {
            changeState(agent, AiState::Flee);
        }

        switch (agent.state) {
            case AiState::Idle: {
                if (agent.target.isValid()) {
                    changeState(agent, AiState::Chase);
                    break;
                }
                if (agent.profile.patrol && agent.profile.patrolRadius > 0.0f) {
                    const tilemap::TileCoord home = m_map.worldToGrid(agent.home);
                    const int span = static_cast<int>(agent.profile.patrolRadius / 16.0f);
                    tilemap::TileCoord patrol = offsetCell(home, span, 0);
                    if (!m_map.isWalkable(patrol.x, patrol.y)) {
                        patrol = offsetCell(home, -span, 0);
                    }
                    if (ensurePath(world, agent, path, patrol)) {
                        changeState(agent, AiState::Patrol);
                    }
                }
                break;
            }
            case AiState::Patrol: {
                if (agent.target.isValid()) {
                    changeState(agent, AiState::Chase);
                    break;
                }
                if (path.hasGoal) {
                    moveAlongPath(world, id, agent, path, deltaSeconds);
                } else {
                    changeState(agent, AiState::Idle);
                }
                break;
            }
            case AiState::Chase: {
                if (!agent.target.isValid()) {
                    changeState(agent, AiState::Return);
                    break;
                }
                const core::Vec2 targetCenter = agentCenter(world, agent.target);
                const float distance = distanceBetween(center, targetCenter);
                if (!agent.targetVisible) {
                    agent.loseTimer += deltaSeconds;
                    if (agent.loseTimer >= agent.profile.loseTargetSeconds) {
                        agent.target = scene::EntityId{};
                        agent.targetVisible = false;
                        changeState(agent, AiState::Return);
                        break;
                    }
                } else {
                    agent.loseTimer = 0.0f;
                }
                if (distance <= agent.profile.attackRange) {
                    changeState(agent, AiState::Attack);
                    break;
                }
                if (!path.hasGoal || agent.repathTimer <= 0.0f) {
                    const tilemap::TileCoord goal = m_map.worldToGrid(targetCenter);
                    static_cast<void>(ensurePath(world, agent, path, goal));
                    agent.repathTimer = std::max(0.05f, agent.profile.repathSeconds);
                }
                if (path.hasGoal) {
                    moveAlongPath(world, id, agent, path, deltaSeconds);
                } else if (agent.targetVisible &&
                           hasLineOfSight(m_map, cellAt(world, id), m_map.worldToGrid(targetCenter))) {
                    const core::Vec2 delta = targetCenter - center;
                    const float length = glm::length(delta);
                    if (length > 0.001f) {
                        static_cast<void>(
                            m_physics.move(world, id, delta / length * agent.profile.moveSpeed * deltaSeconds));
                        ++m_stats.moves;
                    }
                }
                break;
            }
            case AiState::Attack: {
                if (!agent.target.isValid()) {
                    changeState(agent, AiState::Return);
                    break;
                }
                const core::Vec2 targetCenter = agentCenter(world, agent.target);
                if (distanceBetween(center, targetCenter) > agent.profile.attackRange) {
                    changeState(agent, AiState::Chase);
                    break;
                }
                if (agent.attackCooldown <= 0.0f) {
                    if (m_combat != nullptr && agent.profile.attackDamage > 0) {
                        performAttack(world, id, agent, center, targetCenter);
                        ++m_stats.attacks;
                    }
                    agent.attackCooldown = std::max(0.0f, agent.profile.attackCooldown);
                }
                break;
            }
            case AiState::Flee: {
                if (!agent.target.isValid()) {
                    changeState(agent, AiState::Return);
                    break;
                }
                if (distanceBetween(center, agent.home) <= agent.profile.arriveRadius) {
                    agent.target = scene::EntityId{};
                    agent.targetVisible = false;
                    resetPath(path);
                    changeState(agent, AiState::Idle);
                    break;
                }
                if (!path.hasGoal || agent.repathTimer <= 0.0f) {
                    static_cast<void>(ensurePath(world, agent, path, m_map.worldToGrid(agent.home)));
                    agent.repathTimer = std::max(0.05f, agent.profile.repathSeconds);
                }
                if (path.hasGoal) {
                    moveAlongPath(world, id, agent, path, deltaSeconds);
                } else {
                    changeState(agent, AiState::Idle);
                }
                break;
            }
            case AiState::Return: {
                if (agent.target.isValid() && agent.targetVisible) {
                    changeState(agent, AiState::Chase);
                    break;
                }
                if (distanceBetween(center, agent.home) <= agent.profile.arriveRadius) {
                    resetPath(path);
                    changeState(agent, AiState::Idle);
                    break;
                }
                if (!path.hasGoal) {
                    static_cast<void>(ensurePath(world, agent, path, m_map.worldToGrid(agent.home)));
                }
                if (path.hasGoal) {
                    moveAlongPath(world, id, agent, path, deltaSeconds);
                } else {
                    resetPath(path);
                    changeState(agent, AiState::Idle);
                }
                break;
            }
            default:
                changeState(agent, AiState::Idle);
                break;
        }
    });
}

void AiService::setAggro(scene::World& world, scene::EntityId agentId, scene::EntityId target) {
    AiAgent* agent = world.tryGet<AiAgent>(agentId);
    if (agent == nullptr) {
        return;
    }
    PathState& path = pathFor(agentId);
    resetPath(path);
    agent->target = target;
    agent->targetVisible = target.isValid();
    agent->loseTimer = 0.0f;
    changeState(*agent, target.isValid() ? AiState::Chase : AiState::Idle);
}

AiService::PathState& AiService::pathFor(scene::EntityId agent) {
    for (PathState& path : m_paths) {
        if (path.agent == agent) {
            return path;
        }
    }
    PathState path;
    path.agent = agent;
    m_paths.push_back(std::move(path));
    return m_paths.back();
}

void AiService::resetPath(PathState& path) noexcept {
    path.nodes.clear();
    path.index = 0;
    path.hasGoal = false;
}

core::Vec2 AiService::agentCenter(const scene::World& world, scene::EntityId agent) const {
    const scene::Transform* transform = world.tryGet<scene::Transform>(agent);
    if (transform == nullptr) {
        return core::Vec2{};
    }
    const physics::Collider* collider = world.tryGet<physics::Collider>(agent);
    if (collider != nullptr) {
        return core::center(physics::colliderRect(*transform, *collider));
    }
    return core::Vec2{transform->position.x + 8.0f, transform->position.y + 8.0f};
}

tilemap::TileCoord AiService::cellAt(const scene::World& world, scene::EntityId agent) const {
    return m_map.worldToGrid(agentCenter(world, agent));
}

bool AiService::ensurePath(const scene::World& world, AiAgent&, PathState& path, tilemap::TileCoord goal) {
    if (path.hasGoal && path.goal == goal && path.index < path.nodes.size()) {
        return true;
    }

    const tilemap::TileCoord start = cellAt(world, path.agent);
    if (start == goal) {
        resetPath(path);
        path.goal = goal;
        path.hasGoal = true;
        return true;
    }

    std::vector<tilemap::TileCoord> nodes;
    ++m_stats.pathsRequested;
    if (!findPath(start, goal, nodes)) {
        resetPath(path);
        return false;
    }
    path.nodes = std::move(nodes);
    path.index = 0;
    path.goal = goal;
    path.hasGoal = true;
    ++m_stats.pathsFound;
    m_stats.pathNodes += path.nodes.size();
    return true;
}

void AiService::moveAlongPath(
    scene::World& world, scene::EntityId id, AiAgent& agent, PathState& path, float deltaSeconds) {
    while (path.index < path.nodes.size()) {
        const tilemap::TileCoord waypoint = path.nodes[path.index];
        const core::Vec2 target = core::center(m_map.tileRect(waypoint));
        const core::Vec2 center = agentCenter(world, id);
        const core::Vec2 delta = target - center;
        const float distance = glm::length(delta);
        if (distance <= agent.profile.arriveRadius) {
            ++path.index;
            continue;
        }
        static_cast<void>(m_physics.move(world, id, delta / distance * agent.profile.moveSpeed * deltaSeconds));
        ++m_stats.moves;
        return;
    }
    resetPath(path);
}

void AiService::perceive(scene::World& world, scene::EntityId id, AiAgent& agent, core::Vec2 center) {
    ++m_stats.perceptions;
    scene::EntityId best;
    float bestDistance = std::numeric_limits<float>::max();
    const tilemap::TileCoord agentCell = m_map.worldToGrid(center);

    world.each<AiTargetTag, scene::Transform>([&](scene::EntityId target, const AiTargetTag&, const scene::Transform&) {
        if (target == id || !world.isAlive(target)) {
            return;
        }
        const player::Health* health = world.tryGet<player::Health>(target);
        if (health != nullptr && health->current <= 0) {
            return;
        }
        const core::Vec2 targetCenter = agentCenter(world, target);
        const float distance = distanceBetween(center, targetCenter);
        if (distance > agent.profile.sightRadius || distance >= bestDistance) {
            return;
        }
        if (!hasLineOfSight(m_map, agentCell, m_map.worldToGrid(targetCenter))) {
            return;
        }
        best = target;
        bestDistance = distance;
    });

    if (best.isValid()) {
        agent.target = best;
        agent.targetVisible = true;
        agent.loseTimer = 0.0f;
        ++m_stats.targetsFound;
    } else {
        agent.targetVisible = false;
    }
}

void AiService::performAttack(
    scene::World& world, scene::EntityId id, const AiAgent& agent, core::Vec2 center, core::Vec2 targetCenter) {
    if (agent.profile.attackDamage <= 0 || (!m_attackHandler && m_combat == nullptr)) {
        return;
    }
    core::Vec2 direction = targetCenter - center;
    const float length = glm::length(direction);
    if (length <= 0.001f) {
        direction = core::Vec2{1.0f, 0.0f};
    } else {
        direction /= length;
    }

    combat::AttackHitbox hitbox;
    hitbox.offset = core::Vec2{direction.x * 12.0f, direction.y * 12.0f};
    hitbox.size = core::Vec2{16.0f, 16.0f};
    hitbox.activeFrom = 0.0f;
    hitbox.activeTo = 0.1f;
    hitbox.damage = agent.profile.attackDamage;
    hitbox.targetMask = agent.profile.attackMask;
    hitbox.knockback = direction * 16.0f;
    if (m_attackHandler) {
        m_attackHandler(id, hitbox);
    } else {
        m_combat->spawnAttack(world, id, hitbox);
    }
}

void AiService::changeState(AiAgent& agent, AiState state) {
    if (agent.state == state) {
        return;
    }
    agent.state = state;
    agent.stateTime = 0.0f;
    ++m_stats.stateChanges;
}

}  // namespace fa::ai
