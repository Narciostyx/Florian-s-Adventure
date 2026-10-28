#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "fa/content/ContentId.hpp"
#include "fa/content/ContentTypes.hpp"
#include "fa/inventory/ItemStack.hpp"

namespace fa::economy {

/// \file
/// Value types for M24: shop stock, processing jobs, shipping and events.
///
/// Recipes come from M15 `RecipeDef`; shops are a small M24-local table because
/// M15 does not define shop documents yet.

namespace economy_error {

/// \brief M24 owns the 7300 block.
inline constexpr int kNotLoaded = 7300;
inline constexpr int kUnknownShop = 7301;
inline constexpr int kUnknownItem = 7302;
inline constexpr int kUnknownRecipe = 7303;
inline constexpr int kInsufficientGold = 7304;
inline constexpr int kInsufficientItems = 7305;
inline constexpr int kInventoryFull = 7306;
inline constexpr int kDailyLimit = 7307;
inline constexpr int kWrongStation = 7308;
inline constexpr int kWrongSeason = 7309;
inline constexpr int kInvalidCount = 7310;
inline constexpr int kNoReadyJob = 7311;
inline constexpr int kJobNotReady = 7312;
inline constexpr int kInvalidJob = 7313;

}  // namespace economy_error

/// \brief One item in a shop.
struct ShopEntry {
    content::ContentId item;
    int price = 0;
    /// \brief `-1` means unlimited; otherwise units available per day.
    int dailyLimit = -1;
    /// \brief Season bitmask; `Any` means the entry is always stocked.
    std::uint8_t seasons = content::seasonMask(content::Season::Any);
    /// \brief Deterministic daily price swing in `[0, 1]`; 0 keeps the list price.
    float priceSwing = 0.0f;
};

/// \brief One shop definition.
struct ShopDef {
    content::ContentId id;
    std::string name;
    float buyMultiplier = 1.0f;
    float sellMultiplier = 0.5f;
    std::vector<ShopEntry> stock;
};

/// \brief One in-progress processing job.
struct ProcessingJob {
    content::ContentId recipe;
    content::ContentId output;
    int outputCount = 1;
    std::string machineKind;
    int remainingMinutes = 0;
    bool ready = false;
};

/// \brief What one recorded economy action was.
enum class EconomyEventKind : std::uint8_t {
    ItemBought = 0,
    ItemSold,
    CraftStarted,
    CraftReady,
    CraftCollected,
    ItemShipped,
    ShippingSettled,
    DailyRefresh,
    Count,
};

/// \brief Stable name for logs and tests.
[[nodiscard]] std::string_view toString(EconomyEventKind kind) noexcept;

/// \brief One recorded economy transition.
struct EconomyEvent {
    EconomyEventKind kind = EconomyEventKind::ItemBought;
    content::ContentId shop;
    content::ContentId item;
    content::ContentId recipe;
    int amount = 0;
    int gold = 0;
    std::string text;
};

/// \brief Counters for the smoke summary and M27.
struct EconomyStats {
    int gold = 0;
    std::size_t shops = 0;
    std::size_t pendingJobs = 0;
    std::size_t readyJobs = 0;
    std::size_t shippingItems = 0;
    std::uint64_t bought = 0;
    std::uint64_t sold = 0;
    std::uint64_t crafted = 0;
    std::uint64_t shipped = 0;
    int goldEarned = 0;
    int goldSpent = 0;
    std::size_t events = 0;
};

}  // namespace fa::economy
