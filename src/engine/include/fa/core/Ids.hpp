#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace fa::core {

/// Typed wrapper so two ids with the same underlying integer can never be swapped
/// by accident (`ItemId` vs `NpcId`). Zero means "none / invalid"; every module
/// reserves 0 for that and starts real ids at 1.
template <typename Tag, typename T = std::uint32_t>
class StrongId {
public:
    using ValueType = T;
    using TagType = Tag;

    constexpr StrongId() noexcept = default;
    explicit constexpr StrongId(T value) noexcept : m_value(value) {}

    constexpr T value() const noexcept { return m_value; }
    constexpr bool isValid() const noexcept { return m_value != 0; }

    friend constexpr bool operator==(StrongId lhs, StrongId rhs) noexcept { return lhs.m_value == rhs.m_value; }
    friend constexpr bool operator!=(StrongId lhs, StrongId rhs) noexcept { return lhs.m_value != rhs.m_value; }
    friend constexpr bool operator<(StrongId lhs, StrongId rhs) noexcept { return lhs.m_value < rhs.m_value; }
    friend constexpr bool operator>(StrongId lhs, StrongId rhs) noexcept { return lhs.m_value > rhs.m_value; }
    friend constexpr bool operator<=(StrongId lhs, StrongId rhs) noexcept { return lhs.m_value <= rhs.m_value; }
    friend constexpr bool operator>=(StrongId lhs, StrongId rhs) noexcept { return lhs.m_value >= rhs.m_value; }

private:
    T m_value = 0;
};

}  // namespace fa::core

/// Lets StrongId key an unordered_map. Declared for our own type only, which is
/// the one legal way to extend std.
namespace std {

template <typename Tag, typename T>
struct hash<fa::core::StrongId<Tag, T>> {
    std::size_t operator()(const fa::core::StrongId<Tag, T>& id) const noexcept { return std::hash<T>{}(id.value()); }
};

}  // namespace std
