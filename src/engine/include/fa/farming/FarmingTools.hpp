#pragma once

#include <cstdint>

#include "fa/core/Result.hpp"
#include "fa/farming/FarmingService.hpp"
#include "fa/interaction/ToolHandler.hpp"

namespace fa::inventory {
class Inventory;
}

namespace fa::farming {

class DebrisLayer;

/// \file
/// M17 tool adapter for M19.
///
/// The service stays free of scene/tilemap types; this handler is the bridge that
/// receives `ToolUseContext`, interprets the selected seed and forwards to the
/// farming service. Axe/pickaxe clear debris, which is the only world-object
/// interaction v1 implements.

/// \brief Per-tool counters for the smoke summary.
struct FarmingToolStats {
    std::uint64_t hoeUses = 0;
    std::uint64_t waterUses = 0;
    std::uint64_t seedUses = 0;
    std::uint64_t harvestUses = 0;
    std::uint64_t clearUses = 0;
    std::uint64_t harvestItems = 0;
};

/// \brief Handles hoe, watering can, seed, harvest, axe and pickaxe requests.
class FarmingToolHandler final : public interaction::IToolHandler {
public:
    /// \brief \p inventory receives harvested items and is charged for seeds.
    FarmingToolHandler(FarmingService& service, inventory::Inventory& inventory);

    /// \brief Replaces the inventory that seeds are charged to and harvests go to.
    ///
    /// The composition root calls this in play mode so farming uses the player's
    /// backpack instead of the demo-only inventory.
    void setInventory(inventory::Inventory& inventory) noexcept { m_inventory = &inventory; }

    /// \brief Seed used by the next `ToolKind::Seed` request.
    void setSelectedSeed(const content::ContentId& seed) noexcept { m_seed = seed; }
    [[nodiscard]] const content::ContentId& selectedSeed() const noexcept { return m_seed; }

    /// \brief Optional debris layer for axe/pickaxe cleanup.
    void setDebrisLayer(DebrisLayer* debris) noexcept { m_debris = debris; }

    [[nodiscard]] bool handles(interaction::ToolKind tool) const noexcept override;
    [[nodiscard]] core::Status use(const interaction::ToolUseContext& context) override;

    [[nodiscard]] FarmingToolStats stats() const noexcept { return m_stats; }

private:
    FarmingService& m_service;
    inventory::Inventory* m_inventory = nullptr;
    DebrisLayer* m_debris = nullptr;
    content::ContentId m_seed;
    FarmingToolStats m_stats;
};

}  // namespace fa::farming
