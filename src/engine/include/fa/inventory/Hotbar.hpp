#pragma once

#include <span>
#include <vector>

#include "fa/inventory/Inventory.hpp"

namespace fa::inventory {

/// \file
/// Hotbar view over a fixed-size `Inventory`: a selected slot plus cycle/consume
/// helpers. The inventory contents and the selection are separate state, so a save
/// can snapshot both without a special hotbar format.

/// \brief One selection change.
struct HotbarEvent {
    int previous = 0;
    int selected = 0;
};

/// \brief Selected-slot container used by the player HUD.
class Hotbar {
public:
    explicit Hotbar(int slotCount = 10, int defaultMaxStack = 99, const content::ContentDatabase* content = nullptr);

    [[nodiscard]] Inventory& inventory() noexcept { return m_inventory; }
    [[nodiscard]] const Inventory& inventory() const noexcept { return m_inventory; }
    [[nodiscard]] int selected() const noexcept { return m_selected; }

    /// \brief Selects \p slot. Invalid indices leave the selection unchanged.
    [[nodiscard]] bool select(int slot) noexcept;

    /// \brief Moves the selection by \p delta, wrapping around both ends.
    /// \return The new selected slot.
    [[nodiscard]] int cycle(int delta) noexcept;

    [[nodiscard]] const ItemStack& selectedStack() const noexcept;
    [[nodiscard]] core::Result<ItemStack> consumeSelected(int count = 1);

    [[nodiscard]] std::span<const HotbarEvent> events() const noexcept { return m_events; }
    void clearEvents() noexcept { m_events.clear(); }

private:
    Inventory m_inventory;
    int m_selected = 0;
    std::vector<HotbarEvent> m_events;
};

}  // namespace fa::inventory
