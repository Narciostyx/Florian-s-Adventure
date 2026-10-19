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
        case Facing::NorthEast:
            return "north-east";
        case Facing::SouthEast:
            return "south-east";
        case Facing::SouthWest:
            return "south-west";
        case Facing::NorthWest:
            return "north-west";
        default:
            return "unknown";
    }
}

Facing cardinalFacing(Facing facing) noexcept {
    switch (facing) {
        case Facing::NorthEast:
        case Facing::NorthWest:
            return Facing::North;
        case Facing::SouthEast:
        case Facing::SouthWest:
            return Facing::South;
        case Facing::North:
        case Facing::East:
        case Facing::South:
        case Facing::West:
            return facing;
        case Facing::Count:
            break;
    }
    return Facing::South;
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
    if (move.x == 0.0f && move.y == 0.0f) {
        return fallback;
    }

    // Quantize the angle into eight 45-degree sectors. y grows downward, so
    // sector 0 is East, 1 SouthEast, 2 South, ... 7 NorthEast; negative sectors
    // wrap around. Using the angle rather than axis ratios keeps the boundary
    // exactly at 22.5 degrees from each cardinal.
    constexpr float kSectorRadians = 3.14159265358979323846f / 4.0f;
    const long sector = std::lround(std::atan2(move.y, move.x) / kSectorRadians);
    switch (sector < 0 ? sector + 8 : sector % 8) {
        case 0:
            return Facing::East;
        case 1:
            return Facing::SouthEast;
        case 2:
            return Facing::South;
        case 3:
            return Facing::SouthWest;
        case 4:
            return Facing::West;
        case 5:
            return Facing::NorthWest;
        case 6:
            return Facing::North;
        case 7:
            return Facing::NorthEast;
        default:
            return fallback;
    }
}

core::Vec2 directionVector(Facing facing) noexcept {
    constexpr float kDiagonal = 0.70710678118654752440f;
    switch (facing) {
        case Facing::North:
            return core::Vec2{0.0f, -1.0f};
        case Facing::East:
            return core::Vec2{1.0f, 0.0f};
        case Facing::South:
            return core::Vec2{0.0f, 1.0f};
        case Facing::West:
            return core::Vec2{-1.0f, 0.0f};
        case Facing::NorthEast:
            return core::Vec2{kDiagonal, -kDiagonal};
        case Facing::SouthEast:
            return core::Vec2{kDiagonal, kDiagonal};
        case Facing::SouthWest:
            return core::Vec2{-kDiagonal, kDiagonal};
        case Facing::NorthWest:
            return core::Vec2{-kDiagonal, -kDiagonal};
        default:
            return core::Vec2{0.0f, 0.0f};
    }
}

}  // namespace fa::player
