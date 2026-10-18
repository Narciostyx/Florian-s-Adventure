#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "fa/farming/FarmingTypes.hpp"

namespace fa::farming {

/// \file
/// Per-tile world objects and removable debris.
///
/// Both layers are plain data with linear lookup. The map already checked
/// placement legality in M17; this layer only owns occupancy and the object
/// identity. Chest storage and machine processing belong to M24.

/// \brief Occupancy and identity of placed objects such as chests and furnaces.
class WorldObjectLayer {
public:
    /// \brief Adds \p object when its cell is free. \return false when occupied.
    [[nodiscard]] bool add(WorldObject object);

    [[nodiscard]] bool removeAt(tilemap::TileCoord cell) noexcept;
    [[nodiscard]] const WorldObject* at(tilemap::TileCoord cell) const noexcept;
    [[nodiscard]] bool occupied(tilemap::TileCoord cell) const noexcept { return at(cell) != nullptr; }

    [[nodiscard]] std::span<const WorldObject> objects() const noexcept { return m_objects; }
    [[nodiscard]] std::size_t size() const noexcept { return m_objects.size(); }
    void clear() noexcept { m_objects.clear(); }

private:
    std::vector<WorldObject> m_objects;
};

/// \brief Weeds, stones and twigs that a tool can clear for a yield.
class DebrisLayer {
public:
    /// \brief Adds \p debris when its cell is free. \return false when occupied.
    [[nodiscard]] bool add(Debris debris);

    [[nodiscard]] bool removeAt(tilemap::TileCoord cell) noexcept;
    [[nodiscard]] const Debris* at(tilemap::TileCoord cell) const noexcept;
    [[nodiscard]] bool occupied(tilemap::TileCoord cell) const noexcept { return at(cell) != nullptr; }

    [[nodiscard]] std::span<const Debris> debris() const noexcept { return m_debris; }
    [[nodiscard]] std::size_t size() const noexcept { return m_debris.size(); }
    void clear() noexcept { m_debris.clear(); }

private:
    std::vector<Debris> m_debris;
};

}  // namespace fa::farming
