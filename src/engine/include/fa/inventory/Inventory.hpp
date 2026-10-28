#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "fa/content/ContentId.hpp"
#include "fa/core/Result.hpp"
#include "fa/inventory/ItemStack.hpp"

namespace fa::content {
class ContentDatabase;
}

namespace fa::inventory {

/// \file
/// Fixed-slot inventory with content-aware stacking.
///
/// Slot indices are stable: UI drag/drop swaps contents, it never moves slots. Every
/// mutation records an `InventoryEvent`; the composition root may publish those to
/// M03 or just clear them each frame. `maxStackFor()` reads `ItemDef::maxStack` when
/// a content database is bound, otherwise the constructor's default is used.

/// \brief One recorded inventory mutation.
struct InventoryEvent {
    enum class Kind : std::uint8_t {
        Added,
        Removed,
        Merged,
        Swapped,
        Split,
        Restored,
        Changed,
    };

    Kind kind = Kind::Changed;
    /// \brief Primary slot, or -1 for an aggregate `Changed` event.
    int slot = -1;
    /// \brief Second slot for swaps/splits/merges, otherwise -1.
    int otherSlot = -1;
    content::ContentId id;
    int amount = 0;
};

/// \brief Counts for the smoke summary and debug overlay.
struct InventoryStats {
    int slotCount = 0;
    int usedSlots = 0;
    int totalItems = 0;
    int defaultMaxStack = 0;
    std::size_t events = 0;
};

/// \brief Fixed-capacity item container.
class Inventory {
public:
    /// \param slotCount Clamped to at least 0.
    /// \param defaultMaxStack Clamped to at least 1; used when content has no item.
    /// \param content Borrowed and may be null; setContentDatabase() can bind it later.
    explicit Inventory(int slotCount, int defaultMaxStack = 99, const content::ContentDatabase* content = nullptr);

    Inventory(const Inventory&) = default;
    Inventory& operator=(const Inventory&) = default;
    Inventory(Inventory&&) noexcept = default;
    Inventory& operator=(Inventory&&) noexcept = default;

    [[nodiscard]] int slotCount() const noexcept { return static_cast<int>(m_slots.size()); }
    [[nodiscard]] bool isValidSlot(int slot) const noexcept;
    /// \brief Slot contents, or an empty stack for an out-of-range index.
    [[nodiscard]] const ItemStack& at(int slot) const noexcept;
    [[nodiscard]] std::span<const ItemStack> slots() const noexcept { return m_slots; }
    [[nodiscard]] int defaultMaxStack() const noexcept { return m_defaultMaxStack; }
    [[nodiscard]] const content::ContentDatabase* content() const noexcept { return m_content; }
    void setContentDatabase(const content::ContentDatabase* content) noexcept { m_content = content; }

    /// \brief Max stack for \p id: `ItemDef::maxStack` when known, else the default.
    [[nodiscard]] int maxStackFor(const content::ContentId& id) const noexcept;

    [[nodiscard]] int countOf(const content::ContentId& id) const noexcept;
    /// \brief How many of \p stack the inventory can still accept.
    [[nodiscard]] int spaceFor(const ItemStack& stack) const noexcept;
    [[nodiscard]] int totalItems() const noexcept;
    [[nodiscard]] int usedSlots() const noexcept;
    [[nodiscard]] bool empty() const noexcept { return totalItems() == 0; }

    /// \brief Adds as much of \p stack as fits.
    /// \return Remaining count; 0 means everything was stored.
    [[nodiscard]] core::Result<int> add(const ItemStack& stack);

    /// \brief Adds into one slot only, merging or filling it.
    /// \return Remaining count; an id mismatch is `kItemMismatch`.
    [[nodiscard]] core::Result<int> addToSlot(int slot, const ItemStack& stack);

    /// \brief Removes \p count of \p id from any slots.
    ///
    /// Fails with `kInsufficientItems` and changes nothing when the inventory does
    /// not hold enough. Durability is copied from the first matching slot; v1 does
    /// not split stacks by durability on removal.
    [[nodiscard]] core::Result<ItemStack> remove(const content::ContentId& id, int count);

    [[nodiscard]] core::Result<ItemStack> removeAt(int slot, int count);
    [[nodiscard]] core::Result<ItemStack> takeAt(int slot);
    [[nodiscard]] core::Status swap(int from, int to);

    /// \brief Moves \p amount from one slot into the first empty slot.
    /// \return The destination slot index.
    [[nodiscard]] core::Result<int> split(int from, int amount);

    /// \brief Merges \p from into \p to.
    /// \return The count left in the source slot.
    [[nodiscard]] core::Result<int> merge(int from, int to);

    /// \brief Moves up to \p amount from one slot of this inventory into \p to.
    ///
    /// The destination capacity is computed first, so a full destination moves
    /// nothing. Returns the number moved.
    [[nodiscard]] core::Result<int> transferTo(Inventory& to, int fromSlot, int amount);

    /// \brief Replaces one slot; used by restore/save. An empty stack clears it.
    [[nodiscard]] core::Status setSlot(int slot, const ItemStack& stack);

    /// \brief Replaces every slot from a save snapshot.
    [[nodiscard]] core::Status restore(std::span<const ItemStack> slots);

    void clear() noexcept;

    [[nodiscard]] std::span<const InventoryEvent> events() const noexcept { return m_events; }
    void clearEvents() noexcept { m_events.clear(); }
    [[nodiscard]] InventoryStats stats() const noexcept;

private:
    [[nodiscard]] bool sameKind(const ItemStack& lhs, const ItemStack& rhs) const noexcept;
    [[nodiscard]] int spaceForKind(const ItemStack& stack) const noexcept;
    void pushEvent(InventoryEvent event);

    std::vector<ItemStack> m_slots;
    int m_defaultMaxStack = 99;
    const content::ContentDatabase* m_content = nullptr;
    std::vector<InventoryEvent> m_events;
};

}  // namespace fa::inventory
