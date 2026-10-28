#pragma once

#include "fa/core/Math.hpp"
#include "fa/physics/CollisionLayer.hpp"
#include "fa/scene/Components.hpp"

namespace fa::physics {

/// \file
/// The collision box component and the one function that turns it into world space.
///
/// `Collider` is plain data (conventions section 9): no virtuals, no owning pointers.
/// It is deliberately *not* derived from the sprite size, because sprite frames change
/// per animation while the part of an entity that actually touches the world does not.

/// \brief An axis-aligned collision box attached to an entity.
///
/// The box is never rotated or scaled by `Transform`: `Transform::rotation` and
/// `Transform::scale` are presentation, and pixel art plus an axis-aligned world means
/// a rotated hit box would only ever disagree with the tiles. Only `position` feeds
/// physics.
struct Collider {
    /// \brief Top-left of the box relative to `Transform::position`.
    ///
    /// `Transform::position` is the sprite's top-left (that is what `DrawItem` takes),
    /// so a feet box under a 16x24 character sits at `{3, 18}`. See
    /// docs/architecture/pixel-metrics.md and ADR-0003.
    core::Vec2 offset{0.0f, 0.0f};
    /// \brief Box size in world pixels.
    core::Vec2 size{16.0f, 16.0f};
    /// \brief The single layer this box belongs to.
    CollisionLayer layer = CollisionLayer::World;
    /// \brief The layers this box reacts to.
    CollisionMask mask = maskOf(CollisionLayer::All);
    /// \brief Whether the box only reports overlap and never blocks movement.
    bool isTrigger = false;
};

/// \brief World-space box of `collider` on `transform`.
constexpr core::Rect colliderRect(const scene::Transform& transform, const Collider& collider) noexcept {
    return core::Rect{transform.position.x + collider.offset.x,
                      transform.position.y + collider.offset.y,
                      collider.size.x,
                      collider.size.y};
}

/// \brief Whether the box stops movement, i.e. is not a trigger.
constexpr bool blocksMovement(const Collider& collider) noexcept {
    return !collider.isTrigger;
}

/// \brief Whether two colliders should be tested against each other at all.
///
/// Both directions must agree: a trigger that watches `Player` does not fire for an
/// `Enemy`, unless the enemy masks `Trigger` back.
constexpr bool layersInteract(const Collider& a, const Collider& b) noexcept {
    return matches(maskOf(a.layer), b.mask) && matches(maskOf(b.layer), a.mask);
}

/// \brief The 10x6 feet box of a 16x24 character sprite drawn from its top-left.
///
/// 3 px of side margin and 18 px down puts the box's top edge 6 px above the feet,
/// which is the proportion fixed by ADR-0003.
inline const Collider kPlayerFeetCollider{core::Vec2{3.0f, 18.0f},
                                          core::Vec2{10.0f, 6.0f},
                                          CollisionLayer::Player,
                                          maskOf(CollisionLayer::World) | CollisionLayer::Enemy,
                                          false};

}  // namespace fa::physics
