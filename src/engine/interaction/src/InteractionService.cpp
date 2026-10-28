#include "fa/interaction/InteractionService.hpp"

#include "fa/interaction/Interactable.hpp"
#include "fa/interaction/InteractionError.hpp"
#include "fa/interaction/ToolHandler.hpp"
#include "fa/physics/Collider.hpp"
#include "fa/scene/Components.hpp"
#include "fa/scene/World.hpp"
#include "fa/tilemap/Tilemap.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace fa::interaction {

namespace {

[[nodiscard]] float pointDistance(core::Vec2 lhs, core::Vec2 rhs) noexcept {
    const float dx = rhs.x - lhs.x;
    const float dy = rhs.y - lhs.y;
    return std::sqrt(dx * dx + dy * dy);
}

/// \brief Tools that may target a solid cell (mining/chopping/attacking a wall).
[[nodiscard]] bool toolCanTargetSolid(ToolKind tool) noexcept {
    return tool == ToolKind::Axe || tool == ToolKind::Pickaxe || tool == ToolKind::Sword;
}

[[nodiscard]] core::Rect interactableRect(const scene::Transform& transform,
                                          const Interactable& interactable) noexcept {
    return core::makeRect(transform.position.x + interactable.area.x,
                          transform.position.y + interactable.area.y,
                          interactable.area.w,
                          interactable.area.h);
}

}  // namespace

InteractionService::InteractionService(const tilemap::Tilemap& map, PlacementGrid* placement)
    : m_map(map), m_placement(placement) {}

void InteractionService::addHandler(IToolHandler& handler) {
    if (std::find(m_handlers.begin(), m_handlers.end(), &handler) != m_handlers.end()) {
        return;
    }
    m_handlers.push_back(&handler);
}

core::Vec2 InteractionService::actorCenter(const scene::World& world, scene::EntityId actor) const noexcept {
    const scene::Transform* transform = world.tryGet<scene::Transform>(actor);
    if (transform == nullptr) {
        return core::Vec2{};
    }
    if (const physics::Collider* collider = world.tryGet<physics::Collider>(actor); collider != nullptr) {
        return core::center(physics::colliderRect(*transform, *collider));
    }
    return core::Vec2{transform->position.x + m_options.actorFallbackSize.x * 0.5f,
                      transform->position.y + m_options.actorFallbackSize.y * 0.5f};
}

core::Vec2 InteractionService::targetPoint(const scene::World& world, const GridTarget& target) const noexcept {
    if (target.entity.isValid()) {
        const scene::Transform* transform = world.tryGet<scene::Transform>(target.entity);
        const Interactable* interactable = world.tryGet<Interactable>(target.entity);
        if (transform != nullptr && interactable != nullptr) {
            return core::center(interactableRect(*transform, *interactable));
        }
    }
    return core::center(m_map.tileRect(target.cell));
}

float InteractionService::distanceToTarget(const scene::World& world,
                                           scene::EntityId actor,
                                           const GridTarget& target) const noexcept {
    return pointDistance(actorCenter(world, actor), targetPoint(world, target));
}

bool InteractionService::hasLineOfSight(const tilemap::Tilemap& map,
                                        tilemap::TileCoord from,
                                        tilemap::TileCoord to) noexcept {
    int x = from.x;
    int y = from.y;
    const int dx = to.x >= from.x ? to.x - from.x : from.x - to.x;
    const int dy = to.y >= from.y ? to.y - from.y : from.y - to.y;
    const int stepX = from.x < to.x ? 1 : -1;
    const int stepY = from.y < to.y ? 1 : -1;
    int error = dx - dy;
    const int steps = dx + dy + 1;

    for (int step = 0; step < steps; ++step) {
        if (x == to.x && y == to.y) {
            return true;
        }
        const int doubled = 2 * error;
        int nextX = x;
        int nextY = y;
        if (doubled > -dy) {
            error -= dy;
            nextX += stepX;
        }
        if (doubled < dx) {
            error += dx;
            nextY += stepY;
        }
        if (nextX != x && nextY != y) {
            if (map.isSolidCell(nextX, y) || map.isSolidCell(x, nextY)) {
                return false;
            }
        }
        x = nextX;
        y = nextY;
        if (x == to.x && y == to.y) {
            return true;
        }
        if (map.isSolidCell(x, y)) {
            return false;
        }
    }
    return true;
}

GridTarget
InteractionService::pickTarget(const scene::World& world, scene::EntityId actor, core::Vec2 cursorWorld) const {
    GridTarget target;
    if (!world.isAlive(actor) || world.tryGet<scene::Transform>(actor) == nullptr) {
        return target;
    }

    target.cell = m_map.worldToGrid(cursorWorld);
    if (!m_map.contains(target.cell.x, target.cell.y)) {
        return target;
    }
    target.tile = m_map.tileAt(target.cell.x, target.cell.y, m_options.tileLayer);
    target.point = cursorWorld;
    target.valid = true;

    const core::Vec2 actorCenterPoint = actorCenter(world, actor);

    if (m_options.preferEntities) {
        bool found = false;
        scene::EntityId bestEntity;
        std::uint32_t bestPriority = 0;
        std::uint32_t bestPayload = 0;
        core::Vec2 bestPoint{};
        float bestDistance = 0.0f;

        world.each<scene::Transform, Interactable>(
            [&](scene::EntityId id, const scene::Transform& transform, const Interactable& interactable) {
                if (!interactable.enabled || id == actor) {
                    return;
                }
                const core::Rect area = interactableRect(transform, interactable);
                if (!core::rectContains(area, cursorWorld)) {
                    return;
                }
                const core::Vec2 point = core::center(area);
                const float distance = pointDistance(actorCenterPoint, point);
                const bool better = !found || interactable.priority > bestPriority ||
                                    (interactable.priority == bestPriority &&
                                     (distance < bestDistance || (distance == bestDistance && id < bestEntity)));
                if (better) {
                    found = true;
                    bestEntity = id;
                    bestPriority = interactable.priority;
                    bestPayload = interactable.payload;
                    bestPoint = point;
                    bestDistance = distance;
                }
            });

        if (found) {
            target.entity = bestEntity;
            target.payload = bestPayload;
            target.point = bestPoint;
            target.distance = bestDistance;
        }
    }

    if (!target.entity.isValid()) {
        target.point = core::center(m_map.tileRect(target.cell));
        target.distance = pointDistance(actorCenterPoint, target.point);
    }

    target.reachable = target.distance <= m_options.maxDistance;
    if (target.reachable && m_options.requireLineOfSight) {
        target.reachable = hasLineOfSight(m_map, m_map.worldToGrid(actorCenterPoint), target.cell);
    }
    return target;
}

GridTarget
InteractionService::pickTargetInFront(const scene::World& world, scene::EntityId actor, Facing facing) const {
    GridTarget target;
    if (!world.isAlive(actor) || world.tryGet<scene::Transform>(actor) == nullptr) {
        return target;
    }

    tilemap::TileCoord cell = m_map.worldToGrid(actorCenter(world, actor));
    switch (facing) {
        case Facing::North:
            --cell.y;
            break;
        case Facing::East:
            ++cell.x;
            break;
        case Facing::South:
            ++cell.y;
            break;
        case Facing::West:
            --cell.x;
            break;
        default:
            return target;
    }
    if (!m_map.contains(cell.x, cell.y)) {
        target.cell = cell;
        return target;
    }
    return pickTarget(world, actor, core::center(m_map.tileRect(cell)));
}

core::Status InteractionService::validateInteraction(const scene::World& world,
                                                     const InteractionRequest& request) const {
    if (!world.isAlive(request.actor) || world.tryGet<scene::Transform>(request.actor) == nullptr) {
        return core::errorStatus(interaction_error::kInvalidActor, "interaction actor is not alive");
    }
    if (!request.target.valid || !m_map.contains(request.target.cell.x, request.target.cell.y)) {
        return core::errorStatus(interaction_error::kInvalidTarget, "interaction target is outside the map");
    }

    const float distance = distanceToTarget(world, request.actor, request.target);
    if (distance > m_options.maxDistance) {
        return core::errorStatus(interaction_error::kOutOfRange, "interaction target is out of range");
    }

    if (m_options.requireLineOfSight) {
        const core::Vec2 center = actorCenter(world, request.actor);
        if (!hasLineOfSight(m_map, m_map.worldToGrid(center), request.target.cell)) {
            return core::errorStatus(interaction_error::kBlocked, "a solid cell blocks the interaction");
        }
    }

    if (request.target.entity.isValid()) {
        if (!world.isAlive(request.target.entity)) {
            return core::errorStatus(interaction_error::kInvalidTarget, "interaction entity is not alive");
        }
        const Interactable* interactable = world.tryGet<Interactable>(request.target.entity);
        if (interactable == nullptr || !interactable->enabled) {
            return core::errorStatus(interaction_error::kInvalidTarget, "interaction entity is not interactable");
        }
        return core::okStatus();
    }

    if (!request.target.tile.isValid()) {
        return core::errorStatus(interaction_error::kInvalidTarget, "interaction target has no tile");
    }
    if (!m_map.isWalkable(request.target.cell.x, request.target.cell.y) && !toolCanTargetSolid(request.tool)) {
        return core::errorStatus(interaction_error::kBlocked, "a solid tile cannot be used with this tool");
    }
    return core::okStatus();
}

bool InteractionService::canInteract(const scene::World& world,
                                     scene::EntityId actor,
                                     const GridTarget& target,
                                     ToolKind tool) const {
    InteractionRequest request;
    request.actor = actor;
    request.target = target;
    request.tool = tool;
    return validateInteraction(world, request).isOk();
}

core::Status InteractionService::interact(scene::World& world, const InteractionRequest& request) {
    const core::Status validation = validateInteraction(world, request);
    if (validation.isError()) {
        recordInteraction(InteractionEventKind::InteractRequested, request, validation.error().code);
        return validation;
    }
    recordInteraction(InteractionEventKind::InteractRequested, request, 0);

    IToolHandler* handler = findHandler(request.tool);
    if (handler == nullptr) {
        recordInteraction(InteractionEventKind::ToolUsed, request, interaction_error::kNoHandler);
        return core::errorStatus(interaction_error::kNoHandler, "no tool handler accepts this tool");
    }

    ToolUseContext context{world, m_map, m_placement, request};
    const core::Status status = handler->use(context);
    recordInteraction(InteractionEventKind::ToolUsed, request, status.isError() ? status.error().code : 0);
    return status;
}

core::Status InteractionService::canPlace(const scene::World& world, const PlacementRequest& request) const {
    if (m_placement == nullptr) {
        return core::errorStatus(interaction_error::kNoPlacementGrid, "placement needs a placement grid");
    }
    if (!world.isAlive(request.actor) || world.tryGet<scene::Transform>(request.actor) == nullptr) {
        return core::errorStatus(interaction_error::kInvalidActor, "placement actor is not alive");
    }
    if (!request.owner.isValid() || !world.isAlive(request.owner)) {
        return core::errorStatus(interaction_error::kInvalidOwner, "placement owner is not alive");
    }

    const int cellsX = PlacementGrid::cellsAlong(request.size.x, m_map.tileSize());
    const int cellsY = PlacementGrid::cellsAlong(request.size.y, m_map.tileSize());
    if (cellsX <= 0 || cellsY <= 0) {
        return core::errorStatus(interaction_error::kInvalidFootprint, "placement footprint is empty");
    }
    if (!m_map.contains(request.cell.x, request.cell.y) || !m_placement->contains(request.cell.x, request.cell.y)) {
        return core::errorStatus(interaction_error::kOutOfRange, "placement origin is outside the map");
    }

    const long long right = static_cast<long long>(request.cell.x) + cellsX;
    const long long bottom = static_cast<long long>(request.cell.y) + cellsY;
    if (right > static_cast<long long>(m_map.width()) || bottom > static_cast<long long>(m_map.height()) ||
        right > static_cast<long long>(m_placement->columns()) ||
        bottom > static_cast<long long>(m_placement->rows())) {
        return core::errorStatus(interaction_error::kOutOfRange, "placement footprint crosses the map edge");
    }

    const core::Vec2 center = actorCenter(world, request.actor);
    const core::Vec2 cellCenter = core::center(m_map.tileRect(request.cell));
    if (pointDistance(center, cellCenter) > m_options.maxDistance) {
        return core::errorStatus(interaction_error::kOutOfRange, "placement target is out of range");
    }
    if (m_options.requireLineOfSight) {
        if (!hasLineOfSight(m_map, m_map.worldToGrid(center), request.cell)) {
            return core::errorStatus(interaction_error::kBlocked, "a solid cell blocks the placement");
        }
    }

    for (int row = request.cell.y; row < request.cell.y + cellsY; ++row) {
        for (int column = request.cell.x; column < request.cell.x + cellsX; ++column) {
            if (!m_map.isWalkable(column, row)) {
                return core::errorStatus(interaction_error::kNotPlaceable, "placement cell is not walkable");
            }
            if (m_placement->isOccupied(column, row)) {
                return core::errorStatus(interaction_error::kOccupied, "placement cell is already occupied");
            }
            if (request.requiredArea.has_value()) {
                const std::optional<tilemap::AreaKind> area = m_map.queryArea(tilemap::TileCoord{column, row});
                if (!area.has_value() || area.value() != request.requiredArea.value()) {
                    return core::errorStatus(interaction_error::kInvalidArea, "placement is outside the required area");
                }
            }
        }
    }
    return core::okStatus();
}

PlacementPreview InteractionService::previewPlacement(const scene::World& world,
                                                      const PlacementRequest& request) const {
    PlacementPreview preview;
    preview.bounds = PlacementGrid::footprintRect(m_map, request.cell, request.size);
    const core::Status status = canPlace(world, request);
    preview.valid = status.isOk();
    if (status.isError()) {
        preview.errorCode = status.error().code;
        preview.color = core::Color{1.0f, 0.35f, 0.35f, 0.35f};
    }
    return preview;
}

core::Status InteractionService::requestPlacement(scene::World& world, const PlacementRequest& request) {
    const core::Status status = canPlace(world, request);
    if (status.isError()) {
        recordPlacement(InteractionEventKind::PlacementDenied, request, status.error().code);
        return status;
    }
    if (m_placement == nullptr ||
        !m_placement->place(m_map, request.cell, request.size, request.owner, request.payload)) {
        recordPlacement(InteractionEventKind::PlacementDenied, request, interaction_error::kOccupied);
        return core::errorStatus(interaction_error::kOccupied, "placement cells could not be claimed");
    }
    recordPlacement(InteractionEventKind::PlacementRequested, request, 0);
    return core::okStatus();
}

core::Status InteractionService::requestRemoval(scene::World& world, scene::EntityId actor, tilemap::TileCoord cell) {
    const auto deny = [this, actor, cell](int code) {
        InteractionEvent event;
        event.kind = InteractionEventKind::PlacementDenied;
        event.actor = actor;
        event.cell = cell;
        event.errorCode = code;
        m_events.push_back(event);
        return core::errorStatus(code, "removal rejected");
    };

    if (m_placement == nullptr) {
        return deny(interaction_error::kNoPlacementGrid);
    }
    if (!world.isAlive(actor) || world.tryGet<scene::Transform>(actor) == nullptr) {
        return deny(interaction_error::kInvalidActor);
    }
    if (!m_map.contains(cell.x, cell.y) || !m_placement->contains(cell.x, cell.y)) {
        return deny(interaction_error::kInvalidTarget);
    }

    const core::Vec2 center = actorCenter(world, actor);
    if (pointDistance(center, core::center(m_map.tileRect(cell))) > m_options.maxDistance) {
        return deny(interaction_error::kOutOfRange);
    }
    if (m_options.requireLineOfSight && !hasLineOfSight(m_map, m_map.worldToGrid(center), cell)) {
        return deny(interaction_error::kBlocked);
    }

    const scene::EntityId owner = m_placement->ownerAt(cell.x, cell.y);
    if (!owner.isValid()) {
        return deny(interaction_error::kNotOccupied);
    }
    const std::uint32_t payload = m_placement->payloadAt(cell.x, cell.y);
    const std::size_t removed = m_placement->remove(owner);
    if (removed == 0) {
        return deny(interaction_error::kNotOccupied);
    }

    InteractionEvent event;
    event.kind = InteractionEventKind::RemovalRequested;
    event.actor = actor;
    event.entity = owner;
    event.cell = cell;
    event.payload = payload;
    m_events.push_back(event);
    return core::okStatus();
}

IToolHandler* InteractionService::findHandler(ToolKind tool) noexcept {
    for (IToolHandler* handler : m_handlers) {
        if (handler != nullptr && handler->handles(tool)) {
            return handler;
        }
    }
    return nullptr;
}

void InteractionService::recordInteraction(InteractionEventKind kind,
                                           const InteractionRequest& request,
                                           int errorCode) {
    InteractionEvent event;
    event.kind = kind;
    event.actor = request.actor;
    event.entity = request.target.entity;
    event.cell = request.target.cell;
    event.tool = request.tool;
    event.payload = request.payload;
    event.errorCode = errorCode;
    m_events.push_back(event);
}

void InteractionService::recordPlacement(InteractionEventKind kind, const PlacementRequest& request, int errorCode) {
    InteractionEvent event;
    event.kind = kind;
    event.actor = request.actor;
    event.entity = request.owner;
    event.cell = request.cell;
    event.tool = ToolKind::Placeable;
    event.payload = request.payload;
    event.errorCode = errorCode;
    m_events.push_back(event);
}

}  // namespace fa::interaction
