#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "fa/core/Result.hpp"
#include "fa/farming/FarmingTypes.hpp"

namespace fa::content {
class ContentDatabase;
struct CropDef;
}  // namespace fa::content

namespace fa::inventory {
class Inventory;
struct ItemStack;
}  // namespace fa::inventory

namespace fa::tilemap {
class Tilemap;
}  // namespace fa::tilemap

namespace fa::farming {

/// \file
/// Farmland/crop authority for M19.
///
/// Pure per-tile state: no ECS, no file access and no rendering. The composition
/// root supplies a `Tilemap` for tillability/areas, a `ContentDatabase` for crop
/// definitions, and a `FarmingDayContext` converted from M22 events. Harvest adds
/// the item to the caller's `Inventory`; nothing here reads M18 globals.

/// \brief Hoed cells, crops and daily growth.
class FarmingService {
public:
    explicit FarmingService(const content::ContentDatabase* content = nullptr);

    void setContentDatabase(const content::ContentDatabase* content) noexcept { m_content = content; }
    [[nodiscard]] const content::ContentDatabase* content() const noexcept { return m_content; }

    /// \brief Hoes one tillable, non-solid cell.
    [[nodiscard]] core::Status till(tilemap::TileCoord cell, const tilemap::Tilemap& map);

    /// \brief Waters a tilled cell. Watering an already wet cell is a no-op.
    [[nodiscard]] core::Status water(tilemap::TileCoord cell);

    /// \brief Plants the crop whose `seedItem` equals \p seed.
    ///
    /// Uses `currentSeason()` for the season check. Greenhouse cells bypass it.
    [[nodiscard]] core::Status plant(tilemap::TileCoord cell, const content::ContentId& seed);

    /// \brief Harvests a mature crop and adds one item to \p inventory.
    ///
    /// Regrowing crops keep their tile planted and rewind to the regrow window;
    /// other crops leave the tile tilled and empty.
    [[nodiscard]] core::Result<inventory::ItemStack> harvest(tilemap::TileCoord cell, inventory::Inventory& inventory);

    /// \brief Daily settlement: season check, growth, then drying.
    void onDayChanged(const FarmingDayContext& context);

    void setCurrentSeason(content::Season season) noexcept { m_season = season; }
    [[nodiscard]] content::Season currentSeason() const noexcept { return m_season; }

    /// \brief The crop definition whose `seedItem` matches \p seed, or null.
    [[nodiscard]] const content::CropDef* cropForSeed(const content::ContentId& seed) const noexcept;

    [[nodiscard]] const FarmTileEntry* entryAt(tilemap::TileCoord cell) const noexcept;
    [[nodiscard]] const FarmTile* tileAt(tilemap::TileCoord cell) const noexcept;
    [[nodiscard]] const CropState* cropAt(tilemap::TileCoord cell) const noexcept;
    [[nodiscard]] std::span<const FarmTileEntry> entries() const noexcept { return m_entries; }

    /// \brief Replaces every farmland entry from a save snapshot.
    ///
    /// The caller is responsible for dropping cells outside the current map.
    void restore(std::span<const FarmTileEntry> entries);

    [[nodiscard]] FarmingStats stats() const noexcept;
    [[nodiscard]] std::span<const FarmingEvent> events() const noexcept { return m_events; }
    void clearEvents() noexcept { m_events.clear(); }

    void clear() noexcept;

private:
    [[nodiscard]] const FarmTileEntry* findEntry(tilemap::TileCoord cell) const noexcept;
    [[nodiscard]] FarmTileEntry* findEntry(tilemap::TileCoord cell) noexcept;
    [[nodiscard]] FarmTileEntry& ensureEntry(tilemap::TileCoord cell);

    void updateStage(CropState& crop) const;
    void emit(FarmingEventKind kind, tilemap::TileCoord cell, const content::ContentId& item = {}, int amount = 0);

    const content::ContentDatabase* m_content = nullptr;
    content::Season m_season = content::Season::Spring;
    std::vector<FarmTileEntry> m_entries;
    std::vector<FarmingEvent> m_events;
    FarmingStats m_totals;
};

}  // namespace fa::farming
