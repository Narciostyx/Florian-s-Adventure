#include "fa/inventory/ItemUseService.hpp"

#include "fa/content/ContentDatabase.hpp"
#include "fa/inventory/Inventory.hpp"
#include "fa/inventory/InventoryError.hpp"

namespace fa::inventory {

void ItemUseService::record(ItemUseEventKind kind, int slot, const content::ContentId& id, int errorCode) {
    m_events.push_back(ItemUseEvent{kind, slot, id, errorCode});
}

core::Status ItemUseService::use(Inventory& inventory, int slot, scene::EntityId user) {
    if (!inventory.isValidSlot(slot)) {
        record(ItemUseEventKind::Failed, slot, content::ContentId{}, inventory_error::kInvalidSlot);
        return core::errorStatus(inventory_error::kInvalidSlot, "slot is out of range");
    }

    const ItemStack& stack = inventory.at(slot);
    if (stack.empty()) {
        record(ItemUseEventKind::Failed, slot, stack.id, inventory_error::kSlotEmpty);
        return core::errorStatus(inventory_error::kSlotEmpty, "slot is empty");
    }

    const content::ContentId id = stack.id;
    const content::ItemDef* def = m_content.item(id);
    if (def == nullptr) {
        record(ItemUseEventKind::Failed, slot, id, inventory_error::kUnknownItem);
        return core::errorStatus(inventory_error::kUnknownItem, "item is not in the content database");
    }

    IItemUseHandler* handler = nullptr;
    for (IItemUseHandler* candidate : m_handlers) {
        if (candidate != nullptr && candidate->handles(*def)) {
            handler = candidate;
            break;
        }
    }
    if (handler == nullptr) {
        record(ItemUseEventKind::Failed, slot, id, inventory_error::kNoItemHandler);
        return core::errorStatus(inventory_error::kNoItemHandler, "no handler accepted the item");
    }

    ItemUseContext context{inventory, slot, user, m_content, *def};
    const core::Status status = handler->use(context);
    if (status.isError()) {
        record(ItemUseEventKind::Failed, slot, id, status.error().code);
    } else {
        record(ItemUseEventKind::Used, slot, id, 0);
    }
    return status;
}

ItemUseStats ItemUseService::stats() const noexcept {
    ItemUseStats stats;
    stats.handlers = m_handlers.size();
    for (const ItemUseEvent& event : m_events) {
        if (event.kind == ItemUseEventKind::Used) {
            ++stats.uses;
        } else {
            ++stats.failures;
        }
    }
    return stats;
}

}  // namespace fa::inventory
