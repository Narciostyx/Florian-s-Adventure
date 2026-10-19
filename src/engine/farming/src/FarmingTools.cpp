#include "fa/farming/FarmingTools.hpp"

#include "fa/farming/FarmingWorld.hpp"
#include "fa/inventory/Inventory.hpp"

namespace fa::farming {

FarmingToolHandler::FarmingToolHandler(FarmingService& service, inventory::Inventory& inventory)
    : m_service(service), m_inventory(&inventory) {}

bool FarmingToolHandler::handles(interaction::ToolKind tool) const noexcept {
    switch (tool) {
        case interaction::ToolKind::Hoe:
        case interaction::ToolKind::WateringCan:
        case interaction::ToolKind::Seed:
        case interaction::ToolKind::Harvest:
        case interaction::ToolKind::Axe:
        case interaction::ToolKind::Pickaxe:
            return true;
        default:
            return false;
    }
}

core::Status FarmingToolHandler::use(const interaction::ToolUseContext& context) {
    const tilemap::TileCoord cell = context.request.target.cell;
    switch (context.request.tool) {
        case interaction::ToolKind::Hoe: {
            const core::Status status = m_service.till(cell, context.map);
            if (status.isOk()) {
                ++m_stats.hoeUses;
            }
            return status;
        }
        case interaction::ToolKind::WateringCan: {
            const core::Status status = m_service.water(cell);
            if (status.isOk()) {
                ++m_stats.waterUses;
            }
            return status;
        }
        case interaction::ToolKind::Seed: {
            if (m_inventory == nullptr) {
                return core::errorStatus(farming_error::kInventoryFull, "no inventory is bound");
            }
            if (!m_seed.isValid()) {
                return core::errorStatus(farming_error::kNoSeedSelected, "no seed is selected");
            }
            if (m_inventory->countOf(m_seed) <= 0) {
                return core::errorStatus(farming_error::kNoSeedSelected, "the selected seed is not in the inventory");
            }
            const core::Status planted = m_service.plant(cell, m_seed);
            if (planted.isError()) {
                return planted;
            }
            const core::Result<inventory::ItemStack> removed = m_inventory->remove(m_seed, 1);
            if (removed.isError()) {
                return core::errorStatus(removed.error().code, removed.error().message);
            }
            ++m_stats.seedUses;
            return core::okStatus();
        }
        case interaction::ToolKind::Harvest: {
            if (m_inventory == nullptr) {
                return core::errorStatus(farming_error::kInventoryFull, "no inventory is bound");
            }
            const core::Result<inventory::ItemStack> harvested = m_service.harvest(cell, *m_inventory);
            if (harvested.isError()) {
                return core::errorStatus(harvested.error().code, harvested.error().message);
            }
            ++m_stats.harvestUses;
            m_stats.harvestItems += static_cast<std::uint64_t>(harvested.value().count);
            return core::okStatus();
        }
        case interaction::ToolKind::Axe:
        case interaction::ToolKind::Pickaxe: {
            if (m_debris == nullptr) {
                return core::errorStatus(farming_error::kNoDebris, "no debris layer is bound");
            }
            const Debris* debris = m_debris->at(cell);
            if (debris == nullptr) {
                return core::errorStatus(farming_error::kNoDebris, "the cell has no debris");
            }
            inventory::ItemStack yield;
            yield.id = debris->yield;
            yield.count = debris->amount;
            if (!yield.empty()) {
                if (m_inventory == nullptr) {
                    return core::errorStatus(farming_error::kInventoryFull, "no inventory is bound");
                }
                if (m_inventory->spaceFor(yield) < yield.count) {
                    return core::errorStatus(farming_error::kInventoryFull, "no room for the debris yield");
                }
                const core::Result<int> added = m_inventory->add(yield);
                if (added.isError()) {
                    return core::errorStatus(added.error().code, added.error().message);
                }
                if (added.value() != 0) {
                    return core::errorStatus(farming_error::kInventoryFull, "no room for the debris yield");
                }
            }
            if (!m_debris->removeAt(cell)) {
                return core::errorStatus(farming_error::kNoDebris, "the cell has no debris");
            }
            ++m_stats.clearUses;
            return core::okStatus();
        }
        default:
            return core::errorStatus(core::error_code::kUnsupported, "unsupported farming tool");
    }
}

}  // namespace fa::farming
