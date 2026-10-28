#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "fa/content/ContentId.hpp"
#include "fa/content/ContentTypes.hpp"
#include "fa/tilemap/TileId.hpp"

namespace fa::farming {

/// \file
/// Value types for the farming overlay: farmland, crop instances, daily
/// settlement input, world objects, debris and the counters the app reports.
///
/// Farmland and crops are stored per tile instead of as entities; a large field
/// would otherwise pay ECS storage and iteration costs for data that never moves.
/// Only objects that need per-frame behaviour should become entities.

namespace farming_error {

/// \brief M19 owns the 7040 block; inventory already used up to 7030.
inline constexpr int kNotTillable = 7040;
inline constexpr int kAlreadyTilled = 7041;
inline constexpr int kNotTilled = 7042;
inline constexpr int kAlreadyPlanted = 7043;
inline constexpr int kNoContent = 7044;
inline constexpr int kUnknownSeed = 7045;
inline constexpr int kUnknownCrop = 7046;
inline constexpr int kWrongSeason = 7047;
inline constexpr int kNotHarvestable = 7048;
inline constexpr int kInventoryFull = 7049;
inline constexpr int kDeadCrop = 7050;
inline constexpr int kNoSeedSelected = 7051;
inline constexpr int kNoDebris = 7052;
inline constexpr int kObjectOccupied = 7053;

}  // namespace farming_error

/// \brief Whether one cell has been hoed.
struct FarmTile {
    bool tilled = false;
    bool watered = false;
    /// \brief Set when the cell lies inside an `AreaKind::Greenhouse` region.
    bool greenhouse = false;
    /// \brief Whole days since the last watering; informational only in v1.
    int daysSinceWatered = 0;
};

/// \brief One planted crop. The definition fields are copied at plant time so a
/// crop keeps growing even if the content database is reloaded.
struct CropState {
    content::ContentId crop;
    content::ContentId seed;
    content::ContentId harvestItem;
    /// \brief Days required by each visual/growth stage.
    std::vector<int> stageDays;
    /// \brief `content::Season` bit mask copied from `CropDef`.
    std::uint8_t seasons = 0;
    bool regrow = false;
    int regrowDays = 0;
    /// \brief Fractional days grown; weather multipliers can add less than a day.
    float growthDays = 0.0f;
    int stage = 0;
    bool harvestable = false;
    bool dead = false;
    bool wateredToday = false;
};

/// \brief One farmland cell and the crop on it.
struct FarmTileEntry {
    tilemap::TileCoord cell{};
    FarmTile tile{};
    CropState crop{};
    bool hasCrop = false;
};

/// \brief Everything the daily settlement needs from M22.
///
/// M19 deliberately does not include M22; the composition root converts
/// `CalendarEvent` values into this plain struct.
struct FarmingDayContext {
    /// \brief The season bit to test against `CropState::seasons`.
    content::Season season = content::Season::Spring;
    /// \brief Rain/storm auto-waters every tilled cell for this settlement.
    bool rain = false;
    /// \brief M22 `cropGrowthMultiplier()`; 1.0 is normal growth.
    float growthMultiplier = 1.0f;
};

/// \brief What one recorded farming action was.
enum class FarmingEventKind : std::uint8_t {
    Tilled = 0,
    Watered,
    Planted,
    Grown,
    Harvested,
    Wilted,
    Dried,
    Cleared,
    Count,
};

/// \brief Stable name for logs and tests.
[[nodiscard]] std::string_view toString(FarmingEventKind kind) noexcept;

/// \brief One recorded farming transition.
struct FarmingEvent {
    FarmingEventKind kind = FarmingEventKind::Tilled;
    tilemap::TileCoord cell{};
    content::ContentId item;
    int amount = 0;
};

/// \brief Aggregate counters for the smoke summary and M27.
struct FarmingStats {
    std::size_t tilled = 0;
    std::size_t watered = 0;
    std::size_t crops = 0;
    std::size_t harvestable = 0;
    std::size_t dead = 0;
    std::size_t objects = 0;
    std::size_t debris = 0;
    std::uint64_t tills = 0;
    std::uint64_t waters = 0;
    std::uint64_t plants = 0;
    std::uint64_t harvests = 0;
    std::uint64_t harvestItems = 0;
    std::uint64_t days = 0;
    std::uint64_t grownDays = 0;
    std::uint64_t wilted = 0;
    std::uint64_t cleared = 0;
    std::size_t events = 0;
};

/// \brief Kind of a placed world object.
enum class WorldObjectKind : std::uint8_t {
    Chest = 0,
    Sprinkler,
    Furnace,
    Workbench,
    Sign,
    Count,
};

/// \brief One placed object. Storage and processing behaviour belong to M24.
struct WorldObject {
    content::ContentId def;
    tilemap::TileCoord cell{};
    WorldObjectKind kind = WorldObjectKind::Chest;
    int rotation = 0;
    bool blocking = true;
};

/// \brief Kind of removable debris.
enum class DebrisKind : std::uint8_t {
    Weed = 0,
    Stone,
    Twig,
    Count,
};

/// \brief One debris cell and the item it yields when cleared.
struct Debris {
    tilemap::TileCoord cell{};
    DebrisKind kind = DebrisKind::Weed;
    content::ContentId yield;
    int amount = 1;
};

}  // namespace fa::farming
