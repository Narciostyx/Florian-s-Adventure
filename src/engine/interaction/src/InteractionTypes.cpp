#include "fa/interaction/InteractionTypes.hpp"

namespace fa::interaction {

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

std::string_view toString(ToolKind tool) noexcept {
    switch (tool) {
        case ToolKind::None:
            return "none";
        case ToolKind::Hoe:
            return "hoe";
        case ToolKind::WateringCan:
            return "watering_can";
        case ToolKind::Axe:
            return "axe";
        case ToolKind::Pickaxe:
            return "pickaxe";
        case ToolKind::Sword:
            return "sword";
        case ToolKind::Seed:
            return "seed";
        case ToolKind::Harvest:
            return "harvest";
        case ToolKind::Placeable:
            return "placeable";
        default:
            return "unknown";
    }
}

std::string_view toString(InteractionEventKind kind) noexcept {
    switch (kind) {
        case InteractionEventKind::InteractRequested:
            return "interact_requested";
        case InteractionEventKind::ToolUsed:
            return "tool_used";
        case InteractionEventKind::PlacementRequested:
            return "placement_requested";
        case InteractionEventKind::PlacementDenied:
            return "placement_denied";
        case InteractionEventKind::RemovalRequested:
            return "removal_requested";
        default:
            return "unknown";
    }
}

}  // namespace fa::interaction
