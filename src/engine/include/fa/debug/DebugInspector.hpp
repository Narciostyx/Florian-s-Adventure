#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <fa/core/Math.hpp>
#include <fa/scene/EntityId.hpp>

namespace fa::scene {
class World;
}

namespace fa::debug {

/// \brief Read-only snapshot of one entity for the inspector.
struct EntityInspection {
    scene::EntityId id{};
    core::Vec2 position{};
    core::Vec2 spriteSize{};
    core::Rect collider{};
    bool hasTransform = false;
    bool hasSprite = false;
    bool hasCollider = false;
};

/// \brief Collects up to \p maxEntities entities that have a `Transform`.
///
/// The snapshot copies values and never retains component pointers, so it is safe
/// to hold while gameplay code mutates the world.
[[nodiscard]] std::vector<EntityInspection> inspectWorld(const scene::World& world, std::size_t maxEntities = 64);

/// \brief One-line summary used by the console and the overlay.
[[nodiscard]] std::string formatInspection(const EntityInspection& inspection);

}  // namespace fa::debug
