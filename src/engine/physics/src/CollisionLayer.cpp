#include "fa/physics/CollisionLayer.hpp"

namespace fa::physics {

std::string_view toString(CollisionLayer layer) noexcept {
    switch (layer) {
        case CollisionLayer::None:
            return "None";
        case CollisionLayer::World:
            return "World";
        case CollisionLayer::Player:
            return "Player";
        case CollisionLayer::Enemy:
            return "Enemy";
        case CollisionLayer::PlayerAttack:
            return "PlayerAttack";
        case CollisionLayer::EnemyAttack:
            return "EnemyAttack";
        case CollisionLayer::Interactable:
            return "Interactable";
        case CollisionLayer::Trigger:
            return "Trigger";
        case CollisionLayer::All:
            return "All";
        default:
            return "Unknown";
    }
}

}  // namespace fa::physics
