#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "fa/assets/AssetId.hpp"
#include "fa/content/ContentId.hpp"

namespace fa::content {

/// \file
/// Strongly typed content definitions.
///
/// `extra` is deliberately absent: v1 gives each high-frequency field a real type so
/// validation catches typos at load time. A weak map can be added later for
/// experimental content without changing the stable fields.
///
/// Definitions are read-only values. Runtime entities should store `ContentId`, not
/// pointers, because a hot reload can rebuild the vectors and invalidate pointers.

/// \brief Season bits used by crops and recipes.
enum class Season : std::uint8_t {
    Spring = 1u << 0,
    Summer = 1u << 1,
    Fall = 1u << 2,
    Winter = 1u << 3,
    Any = Spring | Summer | Fall | Winter,
};

/// \brief Mask value for one season.
[[nodiscard]] constexpr std::uint8_t seasonMask(Season season) noexcept {
    return static_cast<std::uint8_t>(season);
}

/// \brief Parses `spring`, `summer`, `fall`, `autumn`, `winter` or `any`.
/// \return 0 when the name is unknown.
[[nodiscard]] std::uint8_t parseSeasonMask(std::string_view name) noexcept;

/// \brief Stable name for logs/UI; `any` for the 15 mask.
[[nodiscard]] std::string_view toString(Season season) noexcept;

/// \brief Whether \p mask includes @p season. `Any` matches any non-zero mask.
[[nodiscard]] bool seasonAllows(std::uint8_t mask, Season season) noexcept;

/// \brief One item definition.
struct ItemDef {
    ContentId id;
    std::string displayName;
    std::string description;
    /// \brief Optional icon; invalid when the item has no art yet.
    assets::AssetId icon;
    int maxStack = 99;
    int sellPrice = 0;
    /// \brief Melee damage when equipped as a weapon; 0 means "not a weapon".
    int damage = 0;
    /// \brief Health restored when consumed; 0 means "no healing".
    int healAmount = 0;
    /// \brief Stamina restored when consumed; 0 means "no stamina effect".
    int staminaRestore = 0;
    /// \brief `seed`, `crop`, `tool`, `weapon`, `material`, `food`, ...
    std::string category;
    /// \brief Optional gameplay tool mapping: `hoe`, `watering_can`, `seed`,
    /// `sword`, `axe`, `pickaxe`, `harvest` or `placeable`.
    ///
    /// Kept as a string so M15 does not depend on M17's `ToolKind`. The composition
    /// root converts it once at the use site; an empty string means "not a tool".
    std::string toolKind;
    std::vector<std::string> tags;
};

/// \brief One crop definition.
struct CropDef {
    ContentId id;
    ContentId seedItem;
    ContentId harvestItem;
    /// \brief Days spent in each stage; must be non-empty and positive.
    std::vector<int> stageDays;
    /// \brief Optional per-stage sprites; when present must match `stageDays`.
    std::vector<assets::AssetId> stageSprites;
    /// \brief Optional sprite shown while `growthDays == 0` (just planted).
    assets::AssetId seedSprite;
    std::uint8_t seasons = seasonMask(Season::Any);
    bool regrow = false;
    int regrowDays = 0;
};

/// \brief An item and an amount; used by recipes and loot.
struct ItemAmount {
    ContentId item;
    int amount = 1;
};

/// \brief One crafting/processing recipe.
///
/// `machineKind` is a content string (`kitchen`, `furnace`, ...); M24 interprets it.
/// `seasonMask` lets ADR-0002's greenhouse bypass work without a separate rule type.
struct RecipeDef {
    ContentId id;
    std::vector<ItemAmount> inputs;
    ItemAmount output;
    int requiredMinutes = 0;
    std::string machineKind;
    std::uint8_t seasons = seasonMask(Season::Any);
};

/// \brief One enemy definition.
struct EnemyDef {
    ContentId id;
    int maxHealth = 100;
    int damage = 0;
    float moveSpeed = 60.0f;
    /// \brief Optional loot table reference; invalid when the enemy drops nothing.
    ContentId lootTable;
    std::vector<std::string> tags;
};

/// \brief One entry in a loot table.
struct LootEntry {
    ContentId item;
    int minAmount = 1;
    int maxAmount = 1;
    /// \brief 0..1 probability.
    float chance = 1.0f;
};

/// \brief A named list of loot entries.
struct LootTableDef {
    ContentId id;
    std::vector<LootEntry> entries;
};

/// \brief Counts and revision data for the debug overlay and the smoke summary.
struct ContentStats {
    std::size_t items = 0;
    std::size_t crops = 0;
    std::size_t recipes = 0;
    std::size_t enemies = 0;
    std::size_t lootTables = 0;
    std::size_t validationErrors = 0;
    int schemaVersion = 0;
    /// \brief Incremented by every successful `loadDocument()`.
    std::uint64_t contentVersion = 0;
};

}  // namespace fa::content
