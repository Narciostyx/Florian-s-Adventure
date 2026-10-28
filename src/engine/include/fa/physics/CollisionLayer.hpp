#pragma once

#include <cstdint>
#include <string_view>

namespace fa::physics {

/// \file
/// Which things collide with which.
///
/// A collider names exactly one layer and one mask. Two colliders interact only when
/// each one's layer is present in the other's mask, so "the player collides with
/// walls but a trigger does not" is expressed on both sides and never guessed from
/// one. Masks are plain 32-bit words, so a gameplay module can add a layer without
/// editing this header.

/// \brief Bit set of layers, as used by `Collider::mask` and the query functions.
using CollisionMask = std::uint32_t;

/// \brief One bit per kind of thing that can collide.
///
/// The numeric values are part of the contract: they are written into `Collider`
/// literals and may end up in a save file, so bits are never renumbered.
enum class CollisionLayer : CollisionMask {
    /// \brief Matches nothing; the default for a collider that should stay inert.
    None = 0u,
    /// \brief Terrain, walls, furniture: whatever blocks movement.
    World = 1u << 0,
    /// \brief The player character.
    Player = 1u << 1,
    /// \brief Hostile actors.
    Enemy = 1u << 2,
    /// \brief The player's melee and projectile hit box.
    PlayerAttack = 1u << 3,
    /// \brief Enemy attack hit boxes.
    EnemyAttack = 1u << 4,
    /// \brief Things the player can press "use" on.
    Interactable = 1u << 5,
    /// \brief Non-blocking volumes that only report enter/leave.
    Trigger = 1u << 6,
    /// \brief Every bit set, for "collide with everything" masks.
    All = 0xFFFFFFFFu,
};

/// \brief Widens one layer into a mask.
constexpr CollisionMask maskOf(CollisionLayer layer) noexcept {
    return static_cast<CollisionMask>(layer);
}

/// \brief Combines two layers into a mask, e.g. `World | Enemy`.
constexpr CollisionMask operator|(CollisionLayer lhs, CollisionLayer rhs) noexcept {
    return maskOf(lhs) | maskOf(rhs);
}

/// \brief Adds one layer to an existing mask, for masks built up in a loop.
constexpr CollisionMask operator|(CollisionMask mask, CollisionLayer layer) noexcept {
    return mask | maskOf(layer);
}

/// \brief Whether a collider on `layer` should be considered by a query using `mask`.
constexpr bool matches(CollisionMask layer, CollisionMask mask) noexcept {
    return (layer & mask) != 0u;
}

/// \brief Stable name for logs and the debug overlay.
std::string_view toString(CollisionLayer layer) noexcept;

}  // namespace fa::physics
