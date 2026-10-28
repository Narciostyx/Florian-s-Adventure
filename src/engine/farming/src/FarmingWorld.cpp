#include "fa/farming/FarmingWorld.hpp"

#include <algorithm>
#include <utility>

namespace fa::farming {

bool WorldObjectLayer::add(WorldObject object) {
    if (occupied(object.cell)) {
        return false;
    }
    m_objects.push_back(std::move(object));
    return true;
}

bool WorldObjectLayer::removeAt(tilemap::TileCoord cell) noexcept {
    const auto found = std::find_if(
        m_objects.begin(), m_objects.end(), [cell](const WorldObject& object) { return object.cell == cell; });
    if (found == m_objects.end()) {
        return false;
    }
    m_objects.erase(found);
    return true;
}

const WorldObject* WorldObjectLayer::at(tilemap::TileCoord cell) const noexcept {
    const auto found = std::find_if(
        m_objects.begin(), m_objects.end(), [cell](const WorldObject& object) { return object.cell == cell; });
    return found != m_objects.end() ? &*found : nullptr;
}

bool DebrisLayer::add(Debris debris) {
    if (occupied(debris.cell)) {
        return false;
    }
    m_debris.push_back(debris);
    return true;
}

bool DebrisLayer::removeAt(tilemap::TileCoord cell) noexcept {
    const auto found =
        std::find_if(m_debris.begin(), m_debris.end(), [cell](const Debris& debris) { return debris.cell == cell; });
    if (found == m_debris.end()) {
        return false;
    }
    m_debris.erase(found);
    return true;
}

const Debris* DebrisLayer::at(tilemap::TileCoord cell) const noexcept {
    const auto found =
        std::find_if(m_debris.begin(), m_debris.end(), [cell](const Debris& debris) { return debris.cell == cell; });
    return found != m_debris.end() ? &*found : nullptr;
}

}  // namespace fa::farming
