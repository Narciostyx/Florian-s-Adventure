#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace fa::scene {

/// \brief Handle to one entity slot.
///
/// `index` addresses the slot, `generation` says which tenant of that slot this is.
/// Destroying an entity bumps the generation, so a handle kept past the destroy can
/// never resolve to the next entity that reuses the slot - it simply stops being
/// alive. That is why no entity is ever referenced by pointer.
///
/// Index 0 is reserved for "no entity" (the same convention `core::StrongId` uses),
/// so the first real entity is `{1, 1}`.
struct EntityId {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;

    /// \brief Generation used by `CommandBuffer` placeholders.
    ///
    /// A deferred create cannot know its slot yet, so it hands out an id tagged with
    /// this generation. It is deliberately *not* alive and must never reach `World`.
    static constexpr std::uint32_t kPendingGeneration = 0xFFFFFFFFu;

    /// \brief A real, currently-usable handle (not necessarily alive any more).
    constexpr bool isValid() const noexcept {
        return index != 0 && generation != 0 && generation != kPendingGeneration;
    }

    /// \brief A `CommandBuffer` placeholder awaiting `apply()`.
    constexpr bool isPending() const noexcept { return generation == kPendingGeneration; }

    friend constexpr bool operator==(const EntityId& lhs, const EntityId& rhs) noexcept {
        return lhs.index == rhs.index && lhs.generation == rhs.generation;
    }

    friend constexpr bool operator!=(const EntityId& lhs, const EntityId& rhs) noexcept { return !(lhs == rhs); }

    friend constexpr bool operator<(const EntityId& lhs, const EntityId& rhs) noexcept {
        return lhs.index != rhs.index ? lhs.index < rhs.index : lhs.generation < rhs.generation;
    }
};

/// \brief Stable text form for logs, e.g. `e12v3`. Never parsed back.
///
/// Returned by value rather than as a view because the text is generated on demand;
/// a shared scratch buffer would make two ids in one log line alias each other.
std::string toString(EntityId id);

}  // namespace fa::scene

namespace std {

template <>
struct hash<fa::scene::EntityId> {
    std::size_t operator()(const fa::scene::EntityId& id) const noexcept {
        return (static_cast<std::size_t>(id.index) << 32) ^ static_cast<std::size_t>(id.generation);
    }
};

}  // namespace std
