#pragma once

#include "fa/core/Result.hpp"
#include "fa/interaction/InteractionTypes.hpp"

namespace fa::scene {
class World;
}

namespace fa::tilemap {
class Tilemap;
}

namespace fa::interaction {

class PlacementGrid;

/// \file
/// Extension point for tools such as hoe, watering can, axe or seed.
///
/// M17 does not know what a hoe or a seed does. It resolves the target, validates
/// range and line of sight, then dispatches to the first handler that accepts the
/// tool. M19/M20 register their handlers at the composition root; a smoke test can
/// register a recording handler and observe the exact request.

/// \brief Everything a handler may read or mutate.
///
/// All pointers are borrowed for the duration of one `use()` call. `placement` is
/// null when the service has no placement grid; handlers that do not place ignore it.
struct ToolUseContext {
    scene::World& world;
    const tilemap::Tilemap& map;
    PlacementGrid* placement = nullptr;
    const InteractionRequest& request;
};

/// \brief One tool implementation.
///
/// `use()` may add components or create entities through `world`; structural changes
/// that must be deferred belong in the caller's `CommandBuffer`, not here.
class IToolHandler {
public:
    IToolHandler() = default;
    virtual ~IToolHandler() = default;
    IToolHandler(const IToolHandler&) = delete;
    IToolHandler& operator=(const IToolHandler&) = delete;
    IToolHandler(IToolHandler&&) = delete;
    IToolHandler& operator=(IToolHandler&&) = delete;

    /// \brief Whether this handler accepts `tool`.
    [[nodiscard]] virtual bool handles(ToolKind tool) const noexcept = 0;

    /// \brief Performs the action. \return `okStatus()` on success.
    [[nodiscard]] virtual core::Status use(const ToolUseContext& context) = 0;
};

}  // namespace fa::interaction
