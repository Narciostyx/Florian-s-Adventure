#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "fa/core/Math.hpp"
#include "fa/core/Result.hpp"
#include "fa/interaction/InteractionTypes.hpp"
#include "fa/interaction/PlacementGrid.hpp"
#include "fa/scene/EntityId.hpp"
#include "fa/tilemap/TileId.hpp"

namespace fa::scene {
class World;
}

namespace fa::tilemap {
class Tilemap;
}

namespace fa::interaction {

class IToolHandler;

/// \file
/// Target selection, interaction validation and grid placement.
///
/// The service is a plain object, not an ECS system: M16 will own the input source
/// and call `pickTarget()`/`interact()` at the right moment, while tests can drive it
/// directly without a frame loop. It never writes `Transform::position` and never
/// creates entities; placement only records occupancy, which M19 turns into objects.
///
/// Events are recorded per call and exposed through `events()`, following M13's
/// trigger pattern. The composition root decides whether they become M03 events.

/// \brief Resolves and validates "the actor uses a tool on a cell/entity".
class InteractionService {
public:
    /// \brief \param map Borrowed and must outlive the service.
    /// \param placement Borrowed occupancy grid; null disables placement.
    explicit InteractionService(const tilemap::Tilemap& map, PlacementGrid* placement = nullptr);
    ~InteractionService() = default;
    InteractionService(const InteractionService&) = delete;
    InteractionService& operator=(const InteractionService&) = delete;
    InteractionService(InteractionService&&) = delete;
    InteractionService& operator=(InteractionService&&) = delete;

    void setOptions(const InteractionOptions& options) noexcept { m_options = options; }
    [[nodiscard]] const InteractionOptions& options() const noexcept { return m_options; }

    void setPlacementGrid(PlacementGrid* placement) noexcept { m_placement = placement; }
    [[nodiscard]] PlacementGrid* placementGrid() const noexcept { return m_placement; }

    /// \brief Registers a borrowed handler. Null pointers and duplicates are ignored.
    void addHandler(IToolHandler& handler);

    /// \brief Removes every handler. The services they came from are not touched.
    void clearHandlers() noexcept { m_handlers.clear(); }
    [[nodiscard]] std::size_t handlerCount() const noexcept { return m_handlers.size(); }

    /// \brief Resolves the target under a world-space cursor point.
    ///
    /// Entities whose `Interactable` rectangle contains the point win over tiles;
    /// among them the highest `priority` wins, then the nearest, then the lowest id.
    [[nodiscard]] GridTarget pickTarget(const scene::World& world, scene::EntityId actor, core::Vec2 cursorWorld) const;

    /// \brief Resolves the adjacent cell in `facing` and then applies `pickTarget()`.
    [[nodiscard]] GridTarget pickTargetInFront(const scene::World& world, scene::EntityId actor, Facing facing) const;

    /// \brief Whether an interaction with `target` using `tool` is legal right now.
    ///
    /// Recomputes distance and line of sight from `actor` instead of trusting a
    /// possibly stale `GridTarget::reachable`.
    [[nodiscard]] bool
    canInteract(const scene::World& world, scene::EntityId actor, const GridTarget& target, ToolKind tool) const;

    /// \brief Validates and dispatches one tool request.
    ///
    /// Records `InteractRequested` for every call, then `ToolUsed` with the handler
    /// result (or `kNoHandler`). Never mutates placement.
    [[nodiscard]] core::Status interact(scene::World& world, const InteractionRequest& request);

    /// \brief Whether a placement is legal. \return an `interaction_error` code on failure.
    ///
    /// Checks: live actor and owner, in-bounds footprint, walkable cells, unoccupied
    /// cells, range, line of sight, and `requiredArea` when set.
    [[nodiscard]] core::Status canPlace(const scene::World& world, const PlacementRequest& request) const;

    /// \brief Builds the highlight rectangle and validity for a placement.
    ///
    /// Returns valid bounds even when the placement is illegal, so the caller can draw
    /// a red preview over the rejected cells.
    [[nodiscard]] PlacementPreview previewPlacement(const scene::World& world, const PlacementRequest& request) const;

    /// \brief Validates and marks a placement. \return an error code on failure.
    ///
    /// On success records `PlacementRequested`; every failure records `PlacementDenied`.
    [[nodiscard]] core::Status requestPlacement(scene::World& world, const PlacementRequest& request);

    /// \brief Removes every cell owned by the object occupying `cell`.
    ///
    /// \return `kNotOccupied` when the cell is empty, otherwise `okStatus()`.
    [[nodiscard]] core::Status requestRemoval(scene::World& world, scene::EntityId actor, tilemap::TileCoord cell);

    /// \brief Events recorded since the last `clearEvents()`.
    [[nodiscard]] std::span<const InteractionEvent> events() const noexcept { return m_events; }
    void clearEvents() noexcept { m_events.clear(); }

    /// \brief Actor centre in world pixels: `Collider` when present, otherwise the fallback size.
    [[nodiscard]] core::Vec2 actorCenter(const scene::World& world, scene::EntityId actor) const noexcept;

    /// \brief Bresenham line-of-sight check between two cells.
    ///
    /// The start and target cells are ignored, which is what lets a pickaxe target a
    /// solid wall. A diagonal step is blocked when either orthogonal neighbour is
    /// solid, so two walls cannot be squeezed through at a corner.
    [[nodiscard]] static bool
    hasLineOfSight(const tilemap::Tilemap& map, tilemap::TileCoord from, tilemap::TileCoord to) noexcept;

private:
    [[nodiscard]] core::Status validateInteraction(const scene::World& world, const InteractionRequest& request) const;
    [[nodiscard]] core::Vec2 targetPoint(const scene::World& world, const GridTarget& target) const noexcept;
    [[nodiscard]] float
    distanceToTarget(const scene::World& world, scene::EntityId actor, const GridTarget& target) const noexcept;
    [[nodiscard]] IToolHandler* findHandler(ToolKind tool) noexcept;

    void recordInteraction(InteractionEventKind kind, const InteractionRequest& request, int errorCode);
    void recordPlacement(InteractionEventKind kind, const PlacementRequest& request, int errorCode);

    const tilemap::Tilemap& m_map;
    PlacementGrid* m_placement = nullptr;
    InteractionOptions m_options;
    std::vector<IToolHandler*> m_handlers;
    std::vector<InteractionEvent> m_events;
};

}  // namespace fa::interaction
