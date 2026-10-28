#pragma once

#include <span>
#include <string_view>

#include "fa/physics/PhysicsWorld.hpp"
#include "fa/scene/System.hpp"
#include "fa/time/FrameContext.hpp"

namespace fa::physics {

/// \file
/// The system that makes collision part of the frame loop.
///
/// `PhysicsWorld` is a plain object so a test or a tool can drive it directly; this
/// adapter only adds "run it once per fixed step". Registering it is what gives a scene
/// collision, and its position in the pipeline is load-bearing: it must run after the
/// controllers that write `Velocity` and before anything that reads final positions.

/// \brief Runs `PhysicsWorld::step` every fixed update.
class PhysicsSystem final : public scene::ISystem {
public:
    /// \brief \param tiles May be null; the pointer is borrowed and must outlive this.
    explicit PhysicsSystem(const ISolidTiles* tiles = nullptr, float cellSize = 64.0f);

    [[nodiscard]] std::string_view name() const noexcept override;

    void onFixedUpdate(scene::World& world, const time::FrameContext& context, scene::CommandBuffer& commands) override;

    [[nodiscard]] PhysicsWorld& physics() noexcept { return m_physics; }
    [[nodiscard]] const PhysicsWorld& physics() const noexcept { return m_physics; }

    /// \brief Overlap changes from the most recent fixed step.
    [[nodiscard]] std::span<const TriggerEvent> triggerEvents() const noexcept { return m_physics.triggerEvents(); }

private:
    PhysicsWorld m_physics;
};

}  // namespace fa::physics
