#pragma once

#include "fa/core/Result.hpp"
#include "fa/inventory/Equipment.hpp"
#include "fa/inventory/Hotbar.hpp"

namespace fa::inventory {

/// \file
/// The three player containers (backpack, hotbar, equipment) plus the movement
/// helpers that implement Shift-transfer. Keeping them together makes the save
/// snapshot and the smoke summary obvious; each container remains independently
/// usable.

/// \brief Aggregated counters for the smoke summary.
struct PlayerInventoryStats {
    InventoryStats backpack;
    InventoryStats hotbar;
    int selected = 0;
    EquipmentStats equipment;
};

/// \brief Player-owned containers and quick-transfer helpers.
class PlayerInventory {
public:
    PlayerInventory(int backpackSlots, int hotbarSlots, const content::ContentDatabase* content = nullptr);

    [[nodiscard]] Inventory& backpack() noexcept { return m_backpack; }
    [[nodiscard]] const Inventory& backpack() const noexcept { return m_backpack; }
    [[nodiscard]] Hotbar& hotbar() noexcept { return m_hotbar; }
    [[nodiscard]] const Hotbar& hotbar() const noexcept { return m_hotbar; }
    [[nodiscard]] Equipment& equipment() noexcept { return m_equipment; }
    [[nodiscard]] const Equipment& equipment() const noexcept { return m_equipment; }

    /// \brief Moves one stack (or \p amount) from the backpack into the hotbar.
    ///
    /// \param amount Negative means "the whole source stack". Returns the number
    /// moved; `kInventoryFull` when the hotbar has no space.
    [[nodiscard]] core::Result<int> moveToHotbar(int backpackSlot, int amount = -1);
    [[nodiscard]] core::Result<int> moveToBackpack(int hotbarSlot, int amount = -1);

    [[nodiscard]] PlayerInventoryStats stats() const noexcept;

private:
    Inventory m_backpack;
    Hotbar m_hotbar;
    Equipment m_equipment;
};

}  // namespace fa::inventory
