#include "fa/inventory/Inventory.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "fa/content/ContentDatabase.hpp"
#include "fa/content/ContentTypes.hpp"
#include "fa/inventory/InventoryError.hpp"

namespace fa::inventory {

namespace {

[[nodiscard]] core::Status validateIncoming(const ItemStack& stack) {
    if (!stack.id.isValid()) {
        return core::errorStatus(inventory_error::kInvalidStack, "item id is invalid");
    }
    if (stack.count <= 0) {
        return core::errorStatus(inventory_error::kInvalidQuantity, "item count must be positive");
    }
    return core::okStatus();
}

}  // namespace

Inventory::Inventory(int slotCount, int defaultMaxStack, const content::ContentDatabase* content)
    : m_defaultMaxStack(defaultMaxStack > 0 ? defaultMaxStack : 1), m_content(content) {
    m_slots.resize(slotCount > 0 ? static_cast<std::size_t>(slotCount) : 0u);
    m_events.reserve(8);
}

bool Inventory::isValidSlot(int slot) const noexcept {
    return slot >= 0 && slot < slotCount();
}

const ItemStack& Inventory::at(int slot) const noexcept {
    static const ItemStack kEmpty{};
    return isValidSlot(slot) ? m_slots[static_cast<std::size_t>(slot)] : kEmpty;
}

int Inventory::maxStackFor(const content::ContentId& id) const noexcept {
    if (m_content != nullptr) {
        if (const content::ItemDef* def = m_content->item(id); def != nullptr && def->maxStack > 0) {
            return def->maxStack;
        }
    }
    return m_defaultMaxStack;
}

int Inventory::countOf(const content::ContentId& id) const noexcept {
    int total = 0;
    for (const ItemStack& slot : m_slots) {
        if (slot.id == id) {
            total += slot.count;
        }
    }
    return total;
}

int Inventory::spaceFor(const ItemStack& stack) const noexcept {
    if (!stack.id.isValid()) {
        return 0;
    }
    return spaceForKind(stack);
}

int Inventory::spaceForKind(const ItemStack& stack) const noexcept {
    const int maxStack = maxStackFor(stack.id);
    int space = 0;
    for (const ItemStack& slot : m_slots) {
        if (slot.empty()) {
            space += maxStack;
        } else if (sameKind(slot, stack)) {
            space += std::max(0, maxStack - slot.count);
        }
    }
    return space;
}

int Inventory::totalItems() const noexcept {
    int total = 0;
    for (const ItemStack& slot : m_slots) {
        if (!slot.empty()) {
            total += slot.count;
        }
    }
    return total;
}

int Inventory::usedSlots() const noexcept {
    int used = 0;
    for (const ItemStack& slot : m_slots) {
        if (!slot.empty()) {
            ++used;
        }
    }
    return used;
}

core::Result<int> Inventory::add(const ItemStack& stack) {
    if (const core::Status status = validateIncoming(stack); status.isError()) {
        return core::Result<int>::err(status.error());
    }

    const int maxStack = maxStackFor(stack.id);
    int remaining = stack.count;
    int added = 0;

    for (int slot = 0; slot < slotCount() && remaining > 0; ++slot) {
        ItemStack& target = m_slots[static_cast<std::size_t>(slot)];
        if (target.empty() || !sameKind(target, stack) || target.count >= maxStack) {
            continue;
        }
        const int moved = std::min(remaining, maxStack - target.count);
        target.count += moved;
        remaining -= moved;
        added += moved;
        pushEvent(InventoryEvent{InventoryEvent::Kind::Merged, slot, -1, stack.id, moved});
    }

    for (int slot = 0; slot < slotCount() && remaining > 0; ++slot) {
        ItemStack& target = m_slots[static_cast<std::size_t>(slot)];
        if (!target.empty()) {
            continue;
        }
        const int moved = std::min(remaining, maxStack);
        target = ItemStack{stack.id, moved, stack.durability};
        remaining -= moved;
        added += moved;
        pushEvent(InventoryEvent{InventoryEvent::Kind::Added, slot, -1, stack.id, moved});
    }

    if (added > 0) {
        pushEvent(InventoryEvent{InventoryEvent::Kind::Changed, -1, -1, stack.id, added});
    }
    return core::Result<int>::ok(remaining);
}

core::Result<int> Inventory::addToSlot(int slot, const ItemStack& stack) {
    if (!isValidSlot(slot)) {
        return core::Result<int>::err(inventory_error::kInvalidSlot, "slot is out of range");
    }
    if (const core::Status status = validateIncoming(stack); status.isError()) {
        return core::Result<int>::err(status.error());
    }

    ItemStack& target = m_slots[static_cast<std::size_t>(slot)];
    if (!target.empty() && !sameKind(target, stack)) {
        return core::Result<int>::err(inventory_error::kItemMismatch, "slot already holds another stack");
    }

    const int maxStack = maxStackFor(stack.id);
    const int current = target.empty() ? 0 : target.count;
    if (current >= maxStack) {
        return core::Result<int>::ok(stack.count);
    }

    const int moved = std::min(stack.count, maxStack - current);
    const auto kind = target.empty() ? InventoryEvent::Kind::Added : InventoryEvent::Kind::Merged;
    if (target.empty()) {
        target = ItemStack{stack.id, moved, stack.durability};
    } else {
        target.count += moved;
    }
    pushEvent(InventoryEvent{kind, slot, -1, stack.id, moved});
    pushEvent(InventoryEvent{InventoryEvent::Kind::Changed, -1, -1, stack.id, moved});
    return core::Result<int>::ok(stack.count - moved);
}

core::Result<ItemStack> Inventory::remove(const content::ContentId& id, int count) {
    if (!id.isValid()) {
        return core::Result<ItemStack>::err(inventory_error::kInvalidStack, "item id is invalid");
    }
    if (count <= 0) {
        return core::Result<ItemStack>::err(inventory_error::kInvalidQuantity, "count must be positive");
    }
    if (countOf(id) < count) {
        return core::Result<ItemStack>::err(inventory_error::kInsufficientItems,
                                            "inventory does not hold enough of the item");
    }

    ItemStack removed{id, 0, -1};
    int remaining = count;
    for (int slot = 0; slot < slotCount() && remaining > 0; ++slot) {
        ItemStack& target = m_slots[static_cast<std::size_t>(slot)];
        if (target.empty() || target.id != id) {
            continue;
        }
        if (removed.count == 0) {
            removed.durability = target.durability;
        }
        const int taken = std::min(remaining, target.count);
        target.count -= taken;
        removed.count += taken;
        remaining -= taken;
        pushEvent(InventoryEvent{InventoryEvent::Kind::Removed, slot, -1, id, taken});
        if (target.count == 0) {
            target = ItemStack{};
        }
    }

    pushEvent(InventoryEvent{InventoryEvent::Kind::Changed, -1, -1, id, removed.count});
    return core::Result<ItemStack>::ok(removed);
}

core::Result<ItemStack> Inventory::removeAt(int slot, int count) {
    if (!isValidSlot(slot)) {
        return core::Result<ItemStack>::err(inventory_error::kInvalidSlot, "slot is out of range");
    }
    if (count <= 0) {
        return core::Result<ItemStack>::err(inventory_error::kInvalidQuantity, "count must be positive");
    }

    ItemStack& target = m_slots[static_cast<std::size_t>(slot)];
    if (target.empty()) {
        return core::Result<ItemStack>::err(inventory_error::kSlotEmpty, "slot is empty");
    }
    if (target.count < count) {
        return core::Result<ItemStack>::err(inventory_error::kInsufficientItems, "slot holds fewer items");
    }

    ItemStack removed = target;
    removed.count = count;
    target.count -= count;
    if (target.count == 0) {
        target = ItemStack{};
    }
    pushEvent(InventoryEvent{InventoryEvent::Kind::Removed, slot, -1, removed.id, count});
    pushEvent(InventoryEvent{InventoryEvent::Kind::Changed, -1, -1, removed.id, count});
    return core::Result<ItemStack>::ok(removed);
}

core::Result<ItemStack> Inventory::takeAt(int slot) {
    if (!isValidSlot(slot)) {
        return core::Result<ItemStack>::err(inventory_error::kInvalidSlot, "slot is out of range");
    }
    if (m_slots[static_cast<std::size_t>(slot)].empty()) {
        return core::Result<ItemStack>::err(inventory_error::kSlotEmpty, "slot is empty");
    }
    return removeAt(slot, m_slots[static_cast<std::size_t>(slot)].count);
}

core::Status Inventory::swap(int from, int to) {
    if (!isValidSlot(from) || !isValidSlot(to)) {
        return core::errorStatus(inventory_error::kInvalidSlot, "slot is out of range");
    }
    if (from == to) {
        return core::okStatus();
    }

    std::swap(m_slots[static_cast<std::size_t>(from)], m_slots[static_cast<std::size_t>(to)]);
    pushEvent(InventoryEvent{InventoryEvent::Kind::Swapped, from, to, m_slots[static_cast<std::size_t>(to)].id, 0});
    pushEvent(InventoryEvent{InventoryEvent::Kind::Changed, from, to, m_slots[static_cast<std::size_t>(to)].id, 0});
    return core::okStatus();
}

core::Result<int> Inventory::split(int from, int amount) {
    if (!isValidSlot(from)) {
        return core::Result<int>::err(inventory_error::kInvalidSlot, "slot is out of range");
    }
    if (amount <= 0) {
        return core::Result<int>::err(inventory_error::kInvalidQuantity, "split amount must be positive");
    }

    ItemStack& source = m_slots[static_cast<std::size_t>(from)];
    if (source.empty()) {
        return core::Result<int>::err(inventory_error::kSlotEmpty, "slot is empty");
    }
    if (amount >= source.count) {
        return core::Result<int>::err(inventory_error::kInvalidQuantity,
                                      "split must leave at least one item in the source");
    }

    int target = -1;
    for (int slot = 0; slot < slotCount(); ++slot) {
        if (m_slots[static_cast<std::size_t>(slot)].empty()) {
            target = slot;
            break;
        }
    }
    if (target < 0) {
        return core::Result<int>::err(inventory_error::kInventoryFull, "no empty slot for the split stack");
    }

    const ItemStack splitStack{source.id, amount, source.durability};
    m_slots[static_cast<std::size_t>(target)] = splitStack;
    source.count -= amount;
    pushEvent(InventoryEvent{InventoryEvent::Kind::Split, from, target, splitStack.id, amount});
    pushEvent(InventoryEvent{InventoryEvent::Kind::Changed, -1, -1, splitStack.id, amount});
    return core::Result<int>::ok(target);
}

core::Result<int> Inventory::merge(int from, int to) {
    if (!isValidSlot(from) || !isValidSlot(to)) {
        return core::Result<int>::err(inventory_error::kInvalidSlot, "slot is out of range");
    }
    if (from == to) {
        return core::Result<int>::err(inventory_error::kInvalidTransfer, "cannot merge a slot into itself");
    }

    ItemStack& source = m_slots[static_cast<std::size_t>(from)];
    ItemStack& target = m_slots[static_cast<std::size_t>(to)];
    if (source.empty() || target.empty()) {
        return core::Result<int>::err(inventory_error::kSlotEmpty, "both slots must hold items");
    }
    if (!sameKind(source, target)) {
        return core::Result<int>::err(inventory_error::kItemMismatch, "slots hold different stacks");
    }

    const int maxStack = maxStackFor(source.id);
    const int space = maxStack - target.count;
    if (space <= 0) {
        return core::Result<int>::err(inventory_error::kStackLimitExceeded, "target stack is already full");
    }

    const int moved = std::min(source.count, space);
    target.count += moved;
    source.count -= moved;
    if (source.count == 0) {
        source = ItemStack{};
    }
    pushEvent(InventoryEvent{InventoryEvent::Kind::Merged, from, to, target.id, moved});
    pushEvent(InventoryEvent{InventoryEvent::Kind::Changed, -1, -1, target.id, moved});
    return core::Result<int>::ok(source.count);
}

core::Result<int> Inventory::transferTo(Inventory& to, int fromSlot, int amount) {
    if (&to == this) {
        return core::Result<int>::err(inventory_error::kInvalidTransfer,
                                      "source and destination are the same inventory");
    }
    if (!isValidSlot(fromSlot)) {
        return core::Result<int>::err(inventory_error::kInvalidSlot, "source slot is out of range");
    }
    if (amount <= 0) {
        return core::Result<int>::err(inventory_error::kInvalidQuantity, "amount must be positive");
    }

    ItemStack& source = m_slots[static_cast<std::size_t>(fromSlot)];
    if (source.empty()) {
        return core::Result<int>::err(inventory_error::kSlotEmpty, "source slot is empty");
    }

    const int capacity = to.spaceForKind(source);
    if (capacity <= 0) {
        return core::Result<int>::err(inventory_error::kInventoryFull, "destination has no space");
    }

    const int moved = std::min({amount, source.count, capacity});
    ItemStack movedStack = source;
    movedStack.count = moved;
    const core::Result<int> added = to.add(movedStack);
    if (added.isError()) {
        return core::Result<int>::err(added.error());
    }
    if (added.value() != 0) {
        return core::Result<int>::err(inventory_error::kInventoryFull,
                                      "destination could not accept the planned amount");
    }

    source.count -= moved;
    if (source.count == 0) {
        source = ItemStack{};
    }
    pushEvent(InventoryEvent{InventoryEvent::Kind::Removed, fromSlot, -1, movedStack.id, moved});
    pushEvent(InventoryEvent{InventoryEvent::Kind::Changed, -1, -1, movedStack.id, moved});
    return core::Result<int>::ok(moved);
}

core::Status Inventory::setSlot(int slot, const ItemStack& stack) {
    if (!isValidSlot(slot)) {
        return core::errorStatus(inventory_error::kInvalidSlot, "slot is out of range");
    }
    if (stack.count <= 0) {
        m_slots[static_cast<std::size_t>(slot)] = ItemStack{};
        pushEvent(InventoryEvent{InventoryEvent::Kind::Restored, slot, -1, stack.id, 0});
        return core::okStatus();
    }
    if (!stack.id.isValid()) {
        return core::errorStatus(inventory_error::kInvalidStack, "item id is invalid");
    }
    if (stack.count > maxStackFor(stack.id)) {
        return core::errorStatus(inventory_error::kStackLimitExceeded, "stack exceeds the item's max stack");
    }

    m_slots[static_cast<std::size_t>(slot)] = stack;
    pushEvent(InventoryEvent{InventoryEvent::Kind::Restored, slot, -1, stack.id, stack.count});
    return core::okStatus();
}

core::Status Inventory::restore(std::span<const ItemStack> slots) {
    if (slots.size() != m_slots.size()) {
        return core::errorStatus(inventory_error::kInvalidSlot, "snapshot slot count does not match the inventory");
    }
    for (const ItemStack& stack : slots) {
        if (stack.count <= 0) {
            continue;
        }
        if (!stack.id.isValid()) {
            return core::errorStatus(inventory_error::kInvalidStack, "snapshot contains an invalid item id");
        }
        if (stack.count > maxStackFor(stack.id)) {
            return core::errorStatus(inventory_error::kStackLimitExceeded,
                                     "snapshot stack exceeds the item's max stack");
        }
    }

    int restored = 0;
    for (std::size_t index = 0; index < slots.size(); ++index) {
        m_slots[index] = slots[index];
        if (!slots[index].empty()) {
            restored += slots[index].count;
            pushEvent(InventoryEvent{
                InventoryEvent::Kind::Restored, static_cast<int>(index), -1, slots[index].id, slots[index].count});
        }
    }
    pushEvent(InventoryEvent{InventoryEvent::Kind::Changed, -1, -1, content::ContentId{}, restored});
    return core::okStatus();
}

void Inventory::clear() noexcept {
    for (ItemStack& slot : m_slots) {
        slot = ItemStack{};
    }
    m_events.clear();
}

InventoryStats Inventory::stats() const noexcept {
    InventoryStats stats;
    stats.slotCount = slotCount();
    stats.usedSlots = usedSlots();
    stats.totalItems = totalItems();
    stats.defaultMaxStack = m_defaultMaxStack;
    stats.events = m_events.size();
    return stats;
}

bool Inventory::sameKind(const ItemStack& lhs, const ItemStack& rhs) const noexcept {
    return !lhs.empty() && !rhs.empty() && lhs.id == rhs.id && lhs.durability == rhs.durability;
}

void Inventory::pushEvent(InventoryEvent event) {
    m_events.push_back(std::move(event));
}

}  // namespace fa::inventory
