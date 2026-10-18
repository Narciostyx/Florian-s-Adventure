#pragma once

#include "fa/content/ContentId.hpp"

namespace fa::inventory {

/// \file
/// A counted item stack. Runtime inventories never store `ItemDef` pointers: content
/// can be reloaded and move its vectors, while a `ContentId` stays valid.

/// \brief One item id, a positive count and optional instance data.
///
/// `durability` is reserved for tools/weapons; `-1` means "no durability" and is the
/// only value v1 writes. Stacks only merge when both id and durability match, so
/// adding durability-aware rules later does not change the merge logic.
struct ItemStack {
    content::ContentId id;
    int count = 0;
    int durability = -1;

    /// \brief True when the stack holds no item.
    [[nodiscard]] bool empty() const noexcept { return count <= 0 || !id.isValid(); }

    /// \brief Convenience for `!empty()`.
    explicit operator bool() const noexcept { return !empty(); }

    friend bool operator==(const ItemStack& lhs, const ItemStack& rhs) noexcept {
        return lhs.id == rhs.id && lhs.count == rhs.count && lhs.durability == rhs.durability;
    }

    friend bool operator!=(const ItemStack& lhs, const ItemStack& rhs) noexcept { return !(lhs == rhs); }
};

}  // namespace fa::inventory
