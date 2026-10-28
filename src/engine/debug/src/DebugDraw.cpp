#include "fa/debug/DebugDraw.hpp"

#include <fa/physics/PhysicsWorld.hpp>
#include <fa/render/IRenderer.hpp>

namespace fa::debug {

void drawPhysicsShapes(render::IRenderer& renderer, std::span<const physics::DebugShape> shapes) {
    for (const physics::DebugShape& shape : shapes) {
        renderer.drawDebugRect(shape.rect, shape.color);
    }
}

}  // namespace fa::debug
