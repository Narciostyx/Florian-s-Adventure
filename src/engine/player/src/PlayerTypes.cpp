#include "fa/player/PlayerTypes.hpp"

#include <cmath>

namespace fa::player {

std::string_view toString(Facing facing) noexcept {
    switch (facing) {
        case Facing::North:
            return "north";
        case Facing::East:
            return "east";
        case Facing::South:
            return "south";
        case Facing::West:
            return "west";
        default:
            return "unknown";
    }
}

std::string_view toString(PlayerState state) noexcept {
    switch (state) {
        case PlayerState::Idle:
            return "idle";
        case PlayerState::Move:
            return "move";
        case PlayerState::UseTool:
            return "use_tool";
        case PlayerState::Attack:
            return "attack";
        case PlayerState::Hurt:
            return "hurt";
        case PlayerState::Dead:
            return "dead";
        case PlayerState::Menu:
            return "menu";
        default:
            return "unknown";
    }
}

std::string_view toString(PlayerActionKind kind) noexcept {
    switch (kind) {
        case PlayerActionKind::UseTool:
            return "use_tool";
        case PlayerActionKind::Attack:
            return "attack";
        case PlayerActionKind::Interact:
            return "interact";
        case PlayerActionKind::OpenInventory:
            return "open_inventory";
        case PlayerActionKind::Dodge:
            return "dodge";
        case PlayerActionKind::ActionRejected:
            return "action_rejected";
        default:
            return "unknown";
    }
}

Facing facingFromVector(core::Vec2 move, Facing fallback) noexcept {
    const float ax = std::fabs(move.x);
    const float ay = std::fabs(move.y);
    if (ax == 0.0f && ay == 0.0f) {
        return fallback;
    }
    if (ax >= ay) {
        return move.x >= 0.0f ? Facing::East : Facing::West;
    }
    return move.y >= 0.0f ? Facing::South : Facing::North;
}

core::Vec2 directionVector(Facing facing) noexcept {
    switch (facing) {
        case Facing::North:
            return core::Vec2{0.0f, -1.0f};
        case Facing::East:
            return core::Vec2{1.0f, 0.0f};
        case Facing::South:
            return core::Vec2{0.0f, 1.0f};
        case Facing::West:
            return core::Vec2{-1.0f, 0.0f};
        default:
            return core::Vec2{0.0f, 0.0f};
    }
}

}  // namespace fa::player
