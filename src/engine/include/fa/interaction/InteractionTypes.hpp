#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "fa/core/Math.hpp"
#include "fa/scene/EntityId.hpp"
#include "fa/tilemap/TileId.hpp"
#include "fa/tilemap/Tilemap.hpp"

namespace fa::interaction {

/// \file
/// Value types shared by target selection, tools and placement.
///
/// `payload` is deliberately a plain integer instead of M15's future `ContentId`.
/// M17 must not depend on the content database (it is not implemented yet), and a
/// handler is the only thing that interprets the value anyway. When M15 lands it can
/// replace the field at the call site without changing any M17 logic.

/// \brief Direction an actor is facing, used by keyboard/gamepad targeting.
enum class Facing : std::uint8_t {
    North,
    East,
    South,
    West,
    Count,
};

/// \brief What a tool action is, independent of the item that performs it.
enum class ToolKind : std::uint8_t {
    None,
    Hoe,
    WateringCan,
    Axe,
    Pickaxe,
    Sword,
    Seed,
    Harvest,
    Placeable,
    Count,
};

/// \brief Stable identifier name for logs, editors and JSON.
[[nodiscard]] std::string_view toString(Facing facing) noexcept;

/// \brief Stable identifier name for logs, tools and tests.
[[nodiscard]] std::string_view toString(ToolKind tool) noexcept;

/// \brief Knobs for one target query.
struct InteractionOptions {
    /// \brief Maximum centre-to-centre distance in world pixels. 24 = 1.5 tiles.
    float maxDistance = 24.0f;
    /// \brief Whether a wall between the actor and the target invalidates the target.
    ///
    /// The target cell itself may be solid (mining a wall); only intermediate cells block.
    bool requireLineOfSight = true;
    /// \brief Whether entities are considered before falling back to the tile.
    bool preferEntities = true;
    /// \brief Layer read by `tileAt()` for the target's tile id.
    int tileLayer = 0;
    /// \brief Actor footprint used when the actor has no `Collider` component.
    core::Vec2 actorFallbackSize{16.0f, 16.0f};
};

/// \brief One resolved interaction target.
struct GridTarget {
    tilemap::TileCoord cell{};
    tilemap::TileId tile{};
    /// \brief Best interactable entity, or invalid when the target is a tile.
    scene::EntityId entity{};
    /// \brief Opaque payload copied from `Interactable::payload`.
    std::uint32_t payload = 0;
    /// \brief World-space aiming point: entity centre or tile centre.
    core::Vec2 point{};
    /// \brief Centre-to-centre distance from the actor.
    float distance = 0.0f;
    /// \brief Whether the query found a cell inside the map.
    bool valid = false;
    /// \brief Whether the target is within range and not blocked by a wall.
    bool reachable = false;
};

/// \brief One tool request against a resolved target.
struct InteractionRequest {
    scene::EntityId actor{};
    GridTarget target{};
    ToolKind tool = ToolKind::None;
    /// \brief Opaque item/seed/payload id interpreted by the registered handler.
    std::uint32_t payload = 0;
};

/// \brief One placement request.
struct PlacementRequest {
    scene::EntityId actor{};
    /// \brief Entity that will own the occupied cells; must be alive.
    scene::EntityId owner{};
    tilemap::TileCoord cell{};
    /// \brief Footprint in world pixels. 24x16 covers two cells horizontally.
    core::Vec2 size{16.0f, 16.0f};
    /// \brief When set, every covered cell must report this area kind.
    std::optional<tilemap::AreaKind> requiredArea;
    /// \brief Opaque object/payload id recorded with the occupancy.
    std::uint32_t payload = 0;
};

/// \brief Result of asking whether a placement is legal.
struct PlacementPreview {
    /// \brief World-space bounds of every covered cell.
    core::Rect bounds{};
    bool valid = false;
    /// \brief `interaction_error` code, or 0 when valid.
    int errorCode = 0;
    /// \brief Green when valid, red when not; the caller draws it.
    core::Color color{0.35f, 1.0f, 0.5f, 0.35f};
};

/// \brief What an interaction call recorded.
enum class InteractionEventKind : std::uint8_t {
    /// \brief A request passed validation (or was rejected; see `errorCode`).
    InteractRequested,
    /// \brief A handler ran; `errorCode` is its result.
    ToolUsed,
    /// \brief A placement was accepted and the footprint marked.
    PlacementRequested,
    /// \brief A placement was rejected.
    PlacementDenied,
    /// \brief An occupied footprint was removed.
    RemovalRequested,
};

/// \brief Stable identifier name for logs and tests.
[[nodiscard]] std::string_view toString(InteractionEventKind kind) noexcept;

/// \brief A recorded interaction outcome.
///
/// M17 records instead of publishing to M03, the same way M13 records trigger
/// events. The composition root decides whether these become bus events.
struct InteractionEvent {
    InteractionEventKind kind = InteractionEventKind::InteractRequested;
    scene::EntityId actor{};
    scene::EntityId entity{};
    tilemap::TileCoord cell{};
    ToolKind tool = ToolKind::None;
    std::uint32_t payload = 0;
    /// \brief 0 on success, otherwise an `interaction_error` code.
    int errorCode = 0;
};

}  // namespace fa::interaction
