#include "fa/inventory/Hotbar.hpp"

namespace fa::inventory {

Hotbar::Hotbar(int slotCount, int defaultMaxStack, const content::ContentDatabase* content)
    : m_inventory(slotCount, defaultMaxStack, content) {
    m_events.reserve(4);
}

bool Hotbar::select(int slot) noexcept {
    if (!m_inventory.isValidSlot(slot)) {
        return false;
    }
    if (slot == m_selected) {
        return true;
    }
    m_events.push_back(HotbarEvent{m_selected, slot});
    m_selected = slot;
    return true;
}

int Hotbar::cycle(int delta) noexcept {
    const int count = m_inventory.slotCount();
    if (count <= 0) {
        return m_selected;
    }
    int next = (m_selected + delta) % count;
    if (next < 0) {
        next += count;
    }
    static_cast<void>(select(next));
    return m_selected;
}

const ItemStack& Hotbar::selectedStack() const noexcept {
    return m_inventory.at(m_selected);
}

core::Result<ItemStack> Hotbar::consumeSelected(int count) {
    return m_inventory.removeAt(m_selected, count);
}

}  // namespace fa::inventory
