#pragma once

#include <cstdint>
#include <string_view>

#include "fa/core/Math.hpp"
#include "fa/player/PlayerController.hpp"
#include "fa/scene/System.hpp"

namespace fa::input {
class IInput;
}

namespace fa::player {

/// \brief `ISystem` adapter that turns M07 actions into `PlayerIntent`.
///
/// The controller does the work; this class only samples actions and applies the
/// same intent to every `PlayerTag` entity. Register it **before** `PhysicsSystem`
/// so the velocity it writes is integrated in the same fixed step.
class PlayerSystem final : public scene::ISystem {
public:
    explicit PlayerSystem(input::IInput& input, PlayerTuning tuning = {});
    ~PlayerSystem() override = default;

    [[nodiscard]] std::string_view name() const noexcept override;
    void onFixedUpdate(scene::World& world, const time::FrameContext& context, scene::CommandBuffer& commands) override;

    /// \brief Overrides the input axis, for the headless smoke run.
    ///
    /// Action edges still come from `IInput`; only the movement axis is scripted.
    void setScriptedMove(core::Vec2 move) noexcept {
        m_scriptedMove = move;
        m_hasScriptedMove = true;
    }
    void clearScriptedMove() noexcept { m_hasScriptedMove = false; }

    [[nodiscard]] PlayerController& controller() noexcept { return m_controller; }
    [[nodiscard]] const PlayerController& controller() const noexcept { return m_controller; }

private:
    input::IInput& m_input;
    PlayerController m_controller;
    core::Vec2 m_scriptedMove{};
    bool m_hasScriptedMove = false;

    /// Cached action ids so the hot path does no hashing.
    std::uint32_t m_runAction = 0;
    std::uint32_t m_dodgeAction = 0;
    std::uint32_t m_useToolAction = 0;
    std::uint32_t m_attackAction = 0;
    std::uint32_t m_interactAction = 0;
    std::uint32_t m_inventoryAction = 0;
};

}  // namespace fa::player
