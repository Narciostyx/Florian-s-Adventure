#include "fa/physics/PhysicsSystem.hpp"

namespace fa::physics {

PhysicsSystem::PhysicsSystem(const ISolidTiles* tiles, float cellSize) {
    m_physics.setTiles(tiles);
    m_physics.setCellSize(cellSize);
}

std::string_view PhysicsSystem::name() const noexcept {
    return "PhysicsSystem";
}

void PhysicsSystem::onFixedUpdate(scene::World& world,
                                  const time::FrameContext& context,
                                  scene::CommandBuffer& commands) {
    static_cast<void>(commands);
    m_physics.step(world, static_cast<float>(context.fixedDeltaSeconds));
}

}  // namespace fa::physics
