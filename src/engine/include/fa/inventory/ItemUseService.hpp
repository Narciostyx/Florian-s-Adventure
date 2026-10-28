#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "fa/content/ContentId.hpp"
#include "fa/core/Result.hpp"
#include "fa/inventory/ItemUseHandler.hpp"

namespace fa::content {
class ContentDatabase;
}

namespace fa::inventory {

/// \file
/// Dispatches an item use to the first registered handler that accepts its
/// `ItemDef`. Handlers are non-owning; the composition root keeps them alive. Uses
/// are recorded as `ItemUseEvent`s so the caller can publish them to M03 or print
/// them in the smoke summary.

class Inventory;

/// \brief Whether a use succeeded or failed.
enum class ItemUseEventKind : std::uint8_t { Used, Failed };

/// \brief One recorded use attempt.
struct ItemUseEvent {
    ItemUseEventKind kind = ItemUseEventKind::Failed;
    int slot = -1;
    content::ContentId id;
    /// \brief Error code for a failed attempt, 0 on success.
    int errorCode = 0;
};

/// \brief Counts for the smoke summary and debug overlay.
struct ItemUseStats {
    int uses = 0;
    int failures = 0;
    std::size_t handlers = 0;
};

/// \brief Resolves an `ItemStack` to an `ItemDef` and dispatches to a handler.
class ItemUseService {
public:
    explicit ItemUseService(const content::ContentDatabase& content) noexcept : m_content(content) {}

    /// \brief Registers a non-owning handler. First registered wins on conflict.
    void registerHandler(IItemUseHandler& handler) { m_handlers.push_back(&handler); }
    void clearHandlers() noexcept { m_handlers.clear(); }

    /// \brief Uses the stack in \p slot.
    ///
    /// Fails with `kInvalidSlot`, `kSlotEmpty`, `kUnknownItem` or `kNoItemHandler`
    /// before any handler runs, and forwards the handler's own error otherwise.
    [[nodiscard]] core::Status use(Inventory& inventory, int slot, scene::EntityId user);

    [[nodiscard]] std::span<const ItemUseEvent> events() const noexcept { return m_events; }
    void clearEvents() noexcept { m_events.clear(); }
    [[nodiscard]] ItemUseStats stats() const noexcept;

private:
    void record(ItemUseEventKind kind, int slot, const content::ContentId& id, int errorCode);

    const content::ContentDatabase& m_content;
    std::vector<IItemUseHandler*> m_handlers;
    std::vector<ItemUseEvent> m_events;
};

}  // namespace fa::inventory
