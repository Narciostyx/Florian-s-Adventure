#pragma once

#include <array>
#include <cstdint>

#include "fa/core/Result.hpp"
#include "fa/inventory/ItemStack.hpp"

namespace fa::content {
class ContentDatabase;
}

namespace fa::inventory {

/// \file
/// Tool and weapon slots. v1 keeps one stack per slot and requires a count of one;
/// durability is stored but not decremented yet (M17/M20 own tool and combat rules).

/// \brief Equippable slot kind.
enum class EquipmentSlot : std::uint8_t { Tool, Weapon, Count };

/// \brief Equipment contents for the HUD/summary.
struct EquipmentStats {
    bool tool = false;
    bool weapon = false;
};

/// \brief Two fixed equipment slots, independent of the backpack.
class Equipment {
public:
    explicit Equipment(const content::ContentDatabase* content = nullptr) noexcept;

    /// \brief Contents of \p slot, or an empty stack for an invalid slot.
    [[nodiscard]] const ItemStack& at(EquipmentSlot slot) const noexcept;
    [[nodiscard]] bool empty(EquipmentSlot slot) const noexcept { return at(slot).empty(); }
    [[nodiscard]] const content::ContentDatabase* content() const noexcept { return m_content; }

    /// \brief Places \p stack in \p slot and returns what was there before.
    ///
    /// The stack must hold exactly one item; equipment does not stack. The replaced
    /// stack is empty when the slot was free.
    [[nodiscard]] core::Result<ItemStack> equip(EquipmentSlot slot, const ItemStack& stack);

    /// \brief Clears \p slot and returns its contents.
    [[nodiscard]] core::Result<ItemStack> unequip(EquipmentSlot slot);

    [[nodiscard]] EquipmentStats stats() const noexcept;

private:
    static constexpr std::size_t kSlotCount = static_cast<std::size_t>(EquipmentSlot::Count);
    [[nodiscard]] static bool isValid(EquipmentSlot slot) noexcept {
        return static_cast<std::size_t>(slot) < kSlotCount;
    }

    std::array<ItemStack, kSlotCount> m_slots{};
    const content::ContentDatabase* m_content = nullptr;
};

}  // namespace fa::inventory
