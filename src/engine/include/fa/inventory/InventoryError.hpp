#pragma once

namespace fa::inventory::inventory_error {

/// \file
/// Error codes for M18. M17 owns 7000-7012 and M16 owns 7013-7018 in the gameplay
/// block; inventory continues at 7019.

/// \brief Slot index outside `[0, slotCount)`.
inline constexpr int kInvalidSlot = 7019;

/// \brief An item id is invalid or a stack contains no item.
inline constexpr int kInvalidStack = 7020;

/// \brief The requested slot is empty.
inline constexpr int kSlotEmpty = 7021;

/// \brief The inventory does not hold enough of the requested item.
inline constexpr int kInsufficientItems = 7022;

/// \brief No empty slot or spare capacity is available.
inline constexpr int kInventoryFull = 7023;

/// \brief A count/amount was zero or negative where a positive value is required.
inline constexpr int kInvalidQuantity = 7024;

/// \brief The operation requires stacks of the same item and durability.
inline constexpr int kItemMismatch = 7025;

/// \brief The target stack already reached its max stack size.
inline constexpr int kStackLimitExceeded = 7026;

/// \brief Transfer source and destination are the same inventory, or slots coincide.
inline constexpr int kInvalidTransfer = 7027;

/// \brief No `IItemUseHandler` accepted the item.
inline constexpr int kNoItemHandler = 7028;

/// \brief The item has no `ItemDef` in the content database.
inline constexpr int kUnknownItem = 7029;

/// \brief An equipment slot outside `Tool`/`Weapon`.
inline constexpr int kInvalidEquipmentSlot = 7030;

}  // namespace fa::inventory::inventory_error
