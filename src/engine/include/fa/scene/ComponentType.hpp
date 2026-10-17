#pragma once

#include <atomic>
#include <cstdint>

namespace fa::scene {

/// \file
/// Component type keys without RTTI.
///
/// The engine builds with `/GR-`, so `typeid` is unavailable. The bus in M03 solved
/// the same problem the same way: a process-wide counter hands each type a number the
/// first time it is asked for, and a function-local `static` guarantees the number is
/// computed exactly once per type. Inline function statics are shared across
/// translation units, which is what makes the ids match between `World.cpp`,
/// `SpriteExtractor.cpp` and the game.
using ComponentTypeId = std::uint32_t;

/// \brief Reserved for "no component type".
inline constexpr ComponentTypeId kInvalidComponentType = 0;

namespace detail {

/// \brief Hands out the next component type key. Never called directly.
[[nodiscard]] inline ComponentTypeId allocateComponentTypeId() noexcept {
    static std::atomic<ComponentTypeId> counter{1};
    return counter.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace detail

/// \brief Stable key for `T`, identical for every translation unit.
template <typename T>
[[nodiscard]] ComponentTypeId componentTypeIdOf() noexcept {
    static const ComponentTypeId id = detail::allocateComponentTypeId();
    return id;
}

/// \brief Key for a combination of component types, so a future change-detection
/// layer can cache "which entities have exactly these". Order of `Ts` matters.
template <typename... Ts>
[[nodiscard]] ComponentTypeId componentSetIdOf() noexcept {
    ComponentTypeId combined = 17;
    ((combined = combined * 31 + componentTypeIdOf<Ts>()), ...);
    return combined;
}

}  // namespace fa::scene
