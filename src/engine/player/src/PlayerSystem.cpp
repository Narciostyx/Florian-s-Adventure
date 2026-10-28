#include "fa/player/PlayerSystem.hpp"

#include "fa/input/Action.hpp"
#include "fa/input/Input.hpp"
#include "fa/physics/PhysicsWorld.hpp"
#include "fa/scene/World.hpp"

namespace fa::player {

PlayerSystem::PlayerSystem(input::IInput& input, PlayerTuning tuning) : m_input(input), m_controller(tuning) {
    m_runAction = input::actionIdFromName(kActionNameRun);
    m_dodgeAction = input::actionIdFromName(kActionNameDodge);
    m_useToolAction = input::actionIdFromName(kActionNameUseTool);
    m_attackAction = input::actionIdFromName(kActionNameAttack);
    m_interactAction = input::actionIdFromName(kActionNameInteract);
    m_inventoryAction = input::actionIdFromName(kActionNameInventory);
}

std::string_view PlayerSystem::name() const noexcept {
    return "player";
}

void PlayerSystem::onFixedUpdate(scene::World& world,
                                 const time::FrameContext& context,
                                 scene::CommandBuffer& commands) {
    PlayerIntent intent;
    intent.move = m_hasScriptedMove ? m_scriptedMove : m_input.moveAxis();
    intent.run = m_input.isDown(m_runAction);
    intent.useTool = m_input.wasPressed(m_useToolAction);
    intent.attack = m_input.wasPressed(m_attackAction);
    intent.interact = m_input.wasPressed(m_interactAction);
    intent.dodge = m_input.wasPressed(m_dodgeAction);
    intent.openInventory = m_input.wasPressed(m_inventoryAction);

    const float dt = static_cast<float>(context.fixedDeltaSeconds);
    world.each<PlayerTag, scene::Transform, fa::physics::Velocity>(
        [&](scene::EntityId id, PlayerTag&, scene::Transform&, fa::physics::Velocity&) {
            m_controller.update(world, id, intent, dt);
        });
    static_cast<void>(commands);
}

}  // namespace fa::player
