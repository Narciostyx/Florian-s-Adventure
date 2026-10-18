#pragma once

namespace fa::interaction::interaction_error {

/// \file
/// Error codes for M17. Gameplay modules own the 7000 block; interaction takes the
/// first slice so later gameplay modules can continue after 7011 without collisions.

/// \brief The actor is dead or has no `Transform`.
inline constexpr int kInvalidActor = 7000;

/// \brief The target is stale, outside the map, or no longer interactable.
inline constexpr int kInvalidTarget = 7001;

/// \brief The target is farther than the configured interaction distance.
inline constexpr int kOutOfRange = 7002;

/// \brief A wall or another solid cell blocks the interaction.
inline constexpr int kBlocked = 7003;

/// \brief One or more footprint cells are already occupied.
inline constexpr int kOccupied = 7004;

/// \brief A cell cannot hold the requested placement (solid, water, or out of bounds).
inline constexpr int kNotPlaceable = 7005;

/// \brief No registered tool handler accepts the requested tool.
inline constexpr int kNoHandler = 7006;

/// \brief A placement size is not a positive, usable footprint.
inline constexpr int kInvalidFootprint = 7007;

/// \brief The service was built without a `PlacementGrid`.
inline constexpr int kNoPlacementGrid = 7008;

/// \brief The cell has no placed object to remove.
inline constexpr int kNotOccupied = 7009;

/// \brief The placement is outside the required area kind (for example a greenhouse).
inline constexpr int kInvalidArea = 7010;

/// \brief The entity that would own the placed footprint is not alive.
inline constexpr int kInvalidOwner = 7011;

/// \brief A placement grid size is not usable.
inline constexpr int kInvalidGrid = 7012;

}  // namespace fa::interaction::interaction_error
