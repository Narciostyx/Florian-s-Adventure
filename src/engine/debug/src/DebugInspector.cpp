#include "fa/debug/DebugInspector.hpp"

#include <cstdio>

#include <fa/physics/Collider.hpp>
#include <fa/scene/Components.hpp>
#include <fa/scene/World.hpp>

namespace fa::debug {
namespace {

[[nodiscard]] std::string formatVector(core::Vec2 value) {
    char buffer[64]{};
    std::snprintf(buffer, sizeof(buffer), "(%.1f, %.1f)", static_cast<double>(value.x), static_cast<double>(value.y));
    return buffer;
}

}  // namespace

std::vector<EntityInspection> inspectWorld(const scene::World& world, std::size_t maxEntities) {
    std::vector<EntityInspection> result;
    if (maxEntities == 0) {
        return result;
    }
    result.reserve(maxEntities < 64 ? maxEntities : 64);

    world.each<scene::Transform>([&result, &world, maxEntities](scene::EntityId id, const scene::Transform& transform) {
        if (result.size() >= maxEntities) {
            return;
        }
        EntityInspection inspection;
        inspection.id = id;
        inspection.hasTransform = true;
        inspection.position = transform.position;
        if (const scene::Sprite* sprite = world.tryGet<scene::Sprite>(id); sprite != nullptr) {
            inspection.hasSprite = true;
            inspection.spriteSize = sprite->size;
        }
        if (const physics::Collider* collider = world.tryGet<physics::Collider>(id); collider != nullptr) {
            inspection.hasCollider = true;
            inspection.collider = physics::colliderRect(transform, *collider);
        }
        result.push_back(inspection);
    });
    return result;
}

std::string formatInspection(const EntityInspection& inspection) {
    std::string result = scene::toString(inspection.id);
    if (inspection.hasTransform) {
        result += " pos " + formatVector(inspection.position);
    }
    if (inspection.hasSprite) {
        result += " sprite " + formatVector(inspection.spriteSize);
    }
    if (inspection.hasCollider) {
        char buffer[64]{};
        std::snprintf(buffer,
                      sizeof(buffer),
                      " collider (%.1f, %.1f, %.1f, %.1f)",
                      static_cast<double>(inspection.collider.x),
                      static_cast<double>(inspection.collider.y),
                      static_cast<double>(inspection.collider.w),
                      static_cast<double>(inspection.collider.h));
        result += buffer;
    }
    return result;
}

}  // namespace fa::debug
