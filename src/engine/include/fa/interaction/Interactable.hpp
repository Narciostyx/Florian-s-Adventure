#pragma once

#include <cstdint>

#include "fa/core/Math.hpp"

namespace fa::interaction {

/// \brief Marks an entity as a candidate for cursor/facing target selection.
///
/// The rectangle is local to `Transform::position`, which is the sprite's top-left
/// (the same anchor as `DrawItem` and `Collider`). A 16x24 character therefore uses
/// `{0, 0, 16, 24}`; an NPC with a smaller hit area shrinks it.
///
/// This is plain data: no virtuals, no owning pointers, no behaviour. The rule that
/// entities win over tiles lives in `InteractionService`, not here.
struct Interactable {
    /// \brief Local interaction rectangle relative to the entity's top-left.
    core::Rect area{0.0f, 0.0f, 16.0f, 16.0f};
    /// \brief Higher wins when several entities cover the same point.
    std::uint32_t priority = 0;
    /// \brief Disabled entries are ignored by target selection and `canInteract`.
    bool enabled = true;
    /// \brief Opaque value copied into `GridTarget::payload`.
    std::uint32_t payload = 0;
};

}  // namespace fa::interaction
