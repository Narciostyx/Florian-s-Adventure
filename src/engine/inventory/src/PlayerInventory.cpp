#include "fa/inventory/PlayerInventory.hpp"

#include "fa/inventory/InventoryError.hpp"

namespace fa::inventory {

PlayerInventory::PlayerInventory(int backpackSlots, int hotbarSlots, const content::ContentDatabase* content)
    : m_backpack(backpackSlots, 99, content), m_hotbar(hotbarSlots, 99, content), m_equipment(content) {}

core::Result<int> PlayerInventory::moveToHotbar(int backpackSlot, int amount) {
    if (!m_backpack.isValidSlot(backpackSlot)) {
        return core::Result<int>::err(inventory_error::kInvalidSlot, "backpack slot is out of range");
    }
    if (m_backpack.at(backpackSlot).empty()) {
        return core::Result<int>::err(inventory_error::kSlotEmpty, "backpack slot is empty");
    }

    const int requested = amount < 0 ? m_backpack.at(backpackSlot).count : amount;
    if (requested <= 0) {
        return core::Result<int>::err(inventory_error::kInvalidQuantity, "amount must be positive");
    }
    return m_backpack.transferTo(m_hotbar.inventory(), backpackSlot, requested);
}

core::Result<int> PlayerInventory::moveToBackpack(int hotbarSlot, int amount) {
    if (!m_hotbar.inventory().isValidSlot(hotbarSlot)) {
        return core::Result<int>::err(inventory_error::kInvalidSlot, "hotbar slot is out of range");
    }
    if (m_hotbar.inventory().at(hotbarSlot).empty()) {
        return core::Result<int>::err(inventory_error::kSlotEmpty, "hotbar slot is empty");
    }

    const int requested = amount < 0 ? m_hotbar.inventory().at(hotbarSlot).count : amount;
    if (requested <= 0) {
        return core::Result<int>::err(inventory_error::kInvalidQuantity, "amount must be positive");
    }
    return m_hotbar.inventory().transferTo(m_backpack, hotbarSlot, requested);
}

PlayerInventoryStats PlayerInventory::stats() const noexcept {
    PlayerInventoryStats stats;
    stats.backpack = m_backpack.stats();
    stats.hotbar = m_hotbar.inventory().stats();
    stats.selected = m_hotbar.selected();
    stats.equipment = m_equipment.stats();
    return stats;
}

}  // namespace fa::inventory
