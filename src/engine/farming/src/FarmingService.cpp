#include "fa/farming/FarmingService.hpp"

#include <algorithm>
#include <numeric>
#include <utility>

#include "fa/content/ContentDatabase.hpp"
#include "fa/inventory/Inventory.hpp"
#include "fa/tilemap/Tilemap.hpp"

namespace fa::farming {

std::string_view toString(FarmingEventKind kind) noexcept {
    switch (kind) {
        case FarmingEventKind::Tilled:
            return "tilled";
        case FarmingEventKind::Watered:
            return "watered";
        case FarmingEventKind::Planted:
            return "planted";
        case FarmingEventKind::Grown:
            return "grown";
        case FarmingEventKind::Harvested:
            return "harvested";
        case FarmingEventKind::Wilted:
            return "wilted";
        case FarmingEventKind::Dried:
            return "dried";
        case FarmingEventKind::Cleared:
            return "cleared";
        default:
            return "?";
    }
}

FarmingService::FarmingService(const content::ContentDatabase* content) : m_content(content) {}

core::Status FarmingService::till(tilemap::TileCoord cell, const tilemap::Tilemap& map) {
    if (!map.contains(cell.x, cell.y) || !map.isTillable(cell.x, cell.y) || map.isSolidCell(cell.x, cell.y)) {
        return core::errorStatus(farming_error::kNotTillable, "cell is not tillable");
    }

    FarmTileEntry* entry = findEntry(cell);
    if (entry != nullptr && entry->hasCrop) {
        return core::errorStatus(farming_error::kAlreadyPlanted, "cell already holds a crop");
    }
    if (entry != nullptr && entry->tile.tilled) {
        return core::errorStatus(farming_error::kAlreadyTilled, "cell is already tilled");
    }

    FarmTileEntry& created = ensureEntry(cell);
    created.tile = FarmTile{};
    created.tile.tilled = true;
    created.tile.greenhouse = map.queryArea(cell) == tilemap::AreaKind::Greenhouse;
    created.hasCrop = false;
    ++m_totals.tills;
    emit(FarmingEventKind::Tilled, cell);
    return core::okStatus();
}

core::Status FarmingService::water(tilemap::TileCoord cell) {
    FarmTileEntry* entry = findEntry(cell);
    if (entry == nullptr || !entry->tile.tilled) {
        return core::errorStatus(farming_error::kNotTilled, "cell is not tilled");
    }
    if (entry->tile.watered) {
        return core::okStatus();
    }

    entry->tile.watered = true;
    entry->tile.daysSinceWatered = 0;
    ++m_totals.waters;
    emit(FarmingEventKind::Watered, cell);
    return core::okStatus();
}

core::Status FarmingService::plant(tilemap::TileCoord cell, const content::ContentId& seed) {
    FarmTileEntry* entry = findEntry(cell);
    if (entry == nullptr || !entry->tile.tilled) {
        return core::errorStatus(farming_error::kNotTilled, "cell is not tilled");
    }
    if (entry->hasCrop) {
        return core::errorStatus(farming_error::kAlreadyPlanted, "cell already holds a crop");
    }
    if (m_content == nullptr) {
        return core::errorStatus(farming_error::kNoContent, "farming has no content database");
    }

    const content::CropDef* def = cropForSeed(seed);
    if (def == nullptr) {
        return core::errorStatus(farming_error::kUnknownSeed, "no crop uses this seed");
    }
    if (!entry->tile.greenhouse && !content::seasonAllows(def->seasons, m_season)) {
        return core::errorStatus(farming_error::kWrongSeason, "crop cannot grow in this season");
    }

    CropState crop;
    crop.crop = def->id;
    crop.seed = seed;
    crop.harvestItem = def->harvestItem;
    crop.stageDays = def->stageDays;
    if (crop.stageDays.empty()) {
        crop.stageDays.push_back(1);
    }
    crop.seasons = def->seasons;
    crop.regrow = def->regrow;
    crop.regrowDays = def->regrowDays;
    crop.wateredToday = entry->tile.watered;
    updateStage(crop);

    entry->crop = std::move(crop);
    entry->hasCrop = true;
    ++m_totals.plants;
    emit(FarmingEventKind::Planted, cell, def->id, 1);
    return core::okStatus();
}

core::Result<inventory::ItemStack> FarmingService::harvest(tilemap::TileCoord cell, inventory::Inventory& inventory) {
    FarmTileEntry* entry = findEntry(cell);
    if (entry == nullptr || !entry->hasCrop) {
        return core::Result<inventory::ItemStack>::err(farming_error::kNotHarvestable, "cell has no crop");
    }
    if (entry->crop.dead) {
        return core::Result<inventory::ItemStack>::err(farming_error::kDeadCrop, "crop is dead");
    }
    if (!entry->crop.harvestable) {
        return core::Result<inventory::ItemStack>::err(farming_error::kNotHarvestable, "crop is still growing");
    }

    inventory::ItemStack stack;
    stack.id = entry->crop.harvestItem;
    stack.count = 1;
    if (stack.empty()) {
        return core::Result<inventory::ItemStack>::err(farming_error::kUnknownCrop, "crop has no harvest item");
    }
    if (inventory.spaceFor(stack) < stack.count) {
        return core::Result<inventory::ItemStack>::err(farming_error::kInventoryFull, "no room for the harvest");
    }

    const core::Result<int> added = inventory.add(stack);
    if (added.isError()) {
        return core::Result<inventory::ItemStack>::err(added.error().code, added.error().message);
    }
    if (added.value() != 0) {
        return core::Result<inventory::ItemStack>::err(farming_error::kInventoryFull, "no room for the harvest");
    }

    if (entry->crop.regrow && entry->crop.regrowDays > 0) {
        const int totalDays = std::accumulate(entry->crop.stageDays.begin(), entry->crop.stageDays.end(), 0);
        entry->crop.growthDays = static_cast<float>(std::max(0, totalDays - entry->crop.regrowDays));
        entry->crop.harvestable = false;
        updateStage(entry->crop);
        entry->crop.harvestable = false;
    } else {
        entry->crop = CropState{};
        entry->hasCrop = false;
    }

    ++m_totals.harvests;
    ++m_totals.harvestItems;
    emit(FarmingEventKind::Harvested, cell, stack.id, stack.count);
    return core::Result<inventory::ItemStack>::ok(std::move(stack));
}

void FarmingService::onDayChanged(const FarmingDayContext& context) {
    m_season = context.season;
    ++m_totals.days;

    const float multiplier = std::clamp(context.growthMultiplier, 0.0f, 4.0f);
    for (FarmTileEntry& entry : m_entries) {
        if (!entry.tile.tilled) {
            continue;
        }

        const bool watered = entry.tile.watered || context.rain;
        if (entry.hasCrop) {
            CropState& crop = entry.crop;
            crop.wateredToday = watered;
            if (!crop.dead) {
                const bool seasonOk = entry.tile.greenhouse || content::seasonAllows(crop.seasons, context.season);
                if (!seasonOk) {
                    crop.dead = true;
                    ++m_totals.wilted;
                    emit(FarmingEventKind::Wilted, entry.cell, crop.crop, 1);
                } else if (watered && multiplier > 0.0f) {
                    const int previousStage = crop.stage;
                    crop.growthDays += multiplier;
                    ++m_totals.grownDays;
                    updateStage(crop);
                    if (crop.stage != previousStage) {
                        emit(FarmingEventKind::Grown, entry.cell, crop.crop, crop.stage);
                    }
                }
            }
        }

        if (entry.tile.watered) {
            entry.tile.watered = false;
            entry.tile.daysSinceWatered = 0;
        } else {
            ++entry.tile.daysSinceWatered;
        }
    }
}

void FarmingService::restore(std::span<const FarmTileEntry> entries) {
    m_entries.assign(entries.begin(), entries.end());
    m_events.clear();
}

const content::CropDef* FarmingService::cropForSeed(const content::ContentId& seed) const noexcept {
    if (m_content == nullptr) {
        return nullptr;
    }
    for (const content::CropDef& crop : m_content->crops()) {
        if (crop.seedItem == seed) {
            return &crop;
        }
    }
    return nullptr;
}

const FarmTileEntry* FarmingService::entryAt(tilemap::TileCoord cell) const noexcept {
    return findEntry(cell);
}

const FarmTile* FarmingService::tileAt(tilemap::TileCoord cell) const noexcept {
    const FarmTileEntry* entry = findEntry(cell);
    return entry != nullptr ? &entry->tile : nullptr;
}

const CropState* FarmingService::cropAt(tilemap::TileCoord cell) const noexcept {
    const FarmTileEntry* entry = findEntry(cell);
    return entry != nullptr && entry->hasCrop ? &entry->crop : nullptr;
}

FarmingStats FarmingService::stats() const noexcept {
    FarmingStats result = m_totals;
    result.tilled = 0;
    result.watered = 0;
    result.crops = 0;
    result.harvestable = 0;
    result.dead = 0;
    for (const FarmTileEntry& entry : m_entries) {
        if (!entry.tile.tilled) {
            continue;
        }
        ++result.tilled;
        if (entry.tile.watered) {
            ++result.watered;
        }
        if (entry.hasCrop) {
            ++result.crops;
            if (entry.crop.dead) {
                ++result.dead;
            } else if (entry.crop.harvestable) {
                ++result.harvestable;
            }
        }
    }
    result.events = m_events.size();
    return result;
}

void FarmingService::clear() noexcept {
    m_entries.clear();
    m_events.clear();
    m_totals = FarmingStats{};
    m_season = content::Season::Spring;
}

const FarmTileEntry* FarmingService::findEntry(tilemap::TileCoord cell) const noexcept {
    const auto found = std::find_if(
        m_entries.begin(), m_entries.end(), [cell](const FarmTileEntry& entry) { return entry.cell == cell; });
    return found != m_entries.end() ? &*found : nullptr;
}

FarmTileEntry* FarmingService::findEntry(tilemap::TileCoord cell) noexcept {
    const auto found = std::find_if(
        m_entries.begin(), m_entries.end(), [cell](const FarmTileEntry& entry) { return entry.cell == cell; });
    return found != m_entries.end() ? &*found : nullptr;
}

FarmTileEntry& FarmingService::ensureEntry(tilemap::TileCoord cell) {
    if (FarmTileEntry* existing = findEntry(cell); existing != nullptr) {
        return *existing;
    }
    FarmTileEntry entry;
    entry.cell = cell;
    m_entries.push_back(std::move(entry));
    return m_entries.back();
}

void FarmingService::updateStage(CropState& crop) const {
    int stage = 0;
    float threshold = 0.0f;
    for (const int days : crop.stageDays) {
        threshold += static_cast<float>(days);
        if (crop.growthDays + 1.0e-4f >= threshold) {
            ++stage;
        } else {
            break;
        }
    }
    crop.stage = stage;
    crop.harvestable = stage >= static_cast<int>(crop.stageDays.size());
}

void FarmingService::emit(FarmingEventKind kind, tilemap::TileCoord cell, const content::ContentId& item, int amount) {
    FarmingEvent event;
    event.kind = kind;
    event.cell = cell;
    event.item = item;
    event.amount = amount;
    m_events.push_back(std::move(event));
}

}  // namespace fa::farming
