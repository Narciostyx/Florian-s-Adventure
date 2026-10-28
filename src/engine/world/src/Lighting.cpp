#include "fa/world/Lighting.hpp"

#include <cmath>

namespace fa::world {

std::uint32_t LightSet::add(PointLight light) {
    light.handle = m_nextHandle++;
    const std::uint32_t handle = light.handle;
    m_lights.push_back(light);
    return handle;
}

bool LightSet::remove(std::uint32_t handle) {
    for (auto it = m_lights.begin(); it != m_lights.end(); ++it) {
        if (it->handle == handle) {
            m_lights.erase(it);
            return true;
        }
    }
    return false;
}

void LightSet::clear() noexcept {
    m_lights.clear();
    m_nextHandle = 1;
}

core::Color LightSet::sample(core::Vec2 position) const noexcept {
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    for (const PointLight& light : m_lights) {
        if (light.radius <= 0.0f || light.intensity <= 0.0f) {
            continue;
        }
        const float dx = position.x - light.position.x;
        const float dy = position.y - light.position.y;
        const float distanceSquared = dx * dx + dy * dy;
        const float radiusSquared = light.radius * light.radius;
        if (distanceSquared >= radiusSquared) {
            continue;
        }
        const float falloff = 1.0f - std::sqrt(distanceSquared) / light.radius;
        const float contribution = light.intensity * falloff;
        red += light.color.r * contribution;
        green += light.color.g * contribution;
        blue += light.color.b * contribution;
    }
    return core::Color{core::clamp01(red), core::clamp01(green), core::clamp01(blue), 1.0f};
}

}  // namespace fa::world
