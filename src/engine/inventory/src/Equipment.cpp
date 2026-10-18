#include "fa/inventory/Equipment.hpp"

#include "fa/inventory/InventoryError.hpp"

namespace fa::inventory {

namespace {

const ItemStack kEmptyStack{};

}  // namespace

Equipment::Equipment(const content::ContentDatabase* content) noexcept : m_content(content) {}

const ItemStack& Equipment::at(EquipmentSlot slot) const noexcept {
    return isValid(slot) ? m_slots[static_cast<std::size_t>(slot)] : kEmptyStack;
}

core::Result<ItemStack> Equipment::equip(EquipmentSlot slot, const ItemStack& stack) {
    if (!isValid(slot)) {
        return core::Result<ItemStack>::err(inventory_error::kInvalidEquipmentSlot, "unknown equipment slot");
    }
    if (!stack.id.isValid()) {
        return core::Result<ItemStack>::err(inventory_error::kInvalidStack, "item id is invalid");
    }
    if (stack.count != 1) {
        return core::Result<ItemStack>::err(inventory_error::kInvalidQuantity,
                                            "equipment stack must contain exactly one item");
    }

    ItemStack& target = m_slots[static_cast<std::size_t>(slot)];
    ItemStack replaced = target;
    target = stack;
    return core::Result<ItemStack>::ok(replaced);
}

core::Result<ItemStack> Equipment::unequip(EquipmentSlot slot) {
    if (!isValid(slot)) {
        return core::Result<ItemStack>::err(inventory_error::kInvalidEquipmentSlot, "unknown equipment slot");
    }

    ItemStack& target = m_slots[static_cast<std::size_t>(slot)];
    if (target.empty()) {
        return core::Result<ItemStack>::err(inventory_error::kSlotEmpty, "equipment slot is empty");
    }
    ItemStack removed = target;
    target = ItemStack{};
    return core::Result<ItemStack>::ok(removed);
}

EquipmentStats Equipment::stats() const noexcept {
    EquipmentStats stats;
    stats.tool = !at(EquipmentSlot::Tool).empty();
    stats.weapon = !at(EquipmentSlot::Weapon).empty();
    return stats;
}

}  // namespace fa::inventory
