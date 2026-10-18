#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <fa/core/Math.hpp>

namespace fa::world {

/// \brief Multiplied with world colours; intensity is the luminance used for the
/// screen-darkening approximation.
struct AmbientLight {
    core::Color color{1.0f, 1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
};

/// \brief A temporary world-space light. v1 renders it as a translucent quad.
struct PointLight {
    std::uint32_t handle = 0;
    core::Vec2 position{};
    float radius = 48.0f;
    core::Color color{1.0f, 0.85f, 0.45f, 1.0f};
    float intensity = 1.0f;
};

/// \brief Small handle-based point-light list.
///
/// v1 has no shadowing or additive blend; `sample()` is for callers that tint
/// sprites, and the app draws a translucent quad per light.
class LightSet {
public:
    [[nodiscard]] std::uint32_t add(PointLight light);
    [[nodiscard]] bool remove(std::uint32_t handle);
    void clear() noexcept;

    [[nodiscard]] std::span<const PointLight> lights() const noexcept { return m_lights; }
    [[nodiscard]] core::Color sample(core::Vec2 position) const noexcept;

private:
    std::vector<PointLight> m_lights;
    std::uint32_t m_nextHandle = 1;
};

}  // namespace fa::world
