#pragma once

#include "fa/content/ContentTypes.hpp"
#include "fa/core/Result.hpp"
#include "fa/scene/EntityId.hpp"

namespace fa::content {
class ContentDatabase;
}

namespace fa::inventory {

class Inventory;

/// \file
/// Extension point for consuming, placing or activating an item. M18 validates the
/// slot and resolves the `ItemDef`; the handler owns the actual effect and decides
/// whether to remove the used item (call `inventory.removeAt`).

/// \brief Everything a handler may read or mutate for one use.
///
/// All references are borrowed for the duration of `use()`. `slot` is the slot the
/// use started from; the handler may read it before consuming.
struct ItemUseContext {
    Inventory& inventory;
    int slot;
    scene::EntityId user;
    const content::ContentDatabase& content;
    const content::ItemDef& item;
};

/// \brief One item-use implementation, registered with `ItemUseService`.
class IItemUseHandler {
public:
    IItemUseHandler() = default;
    virtual ~IItemUseHandler() = default;
    IItemUseHandler(const IItemUseHandler&) = delete;
    IItemUseHandler& operator=(const IItemUseHandler&) = delete;
    IItemUseHandler(IItemUseHandler&&) = delete;
    IItemUseHandler& operator=(IItemUseHandler&&) = delete;

    /// \brief Whether this handler accepts the definition.
    [[nodiscard]] virtual bool handles(const content::ItemDef& item) const noexcept = 0;

    /// \brief Performs the use. \return `okStatus()` on success.
    [[nodiscard]] virtual core::Status use(ItemUseContext& context) = 0;
};

}  // namespace fa::inventory
