/// \file
/// \brief Tests for M15: content ids, season masks, JSON loading, table queries,
/// reference validation and error collection. Everything is headless; the database
/// takes text and never touches the file system.
/// See docs/build/m15-content-database.md for the problems found while writing this.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <fa/content/ContentDatabase.hpp>
#include <fa/content/ContentError.hpp>
#include <fa/content/ContentId.hpp>
#include <fa/content/ContentTypes.hpp>
#include <fa/core/Result.hpp>

namespace {

namespace err = fa::content::content_error;

using Catch::Approx;
using fa::content::ContentDatabase;
using fa::content::ContentError;
using fa::content::ContentId;
using fa::content::Season;

constexpr std::string_view kValidDocument = R"({
  "schema_version": 1,
  "items": [
    {"id": "item.turnip_seed", "display_name": "Turnip Seed", "category": "seed",
     "tool_kind": "seed", "max_stack": 99, "sell_price": 5, "tags": ["seed", "spring"]},
    {"id": "item.turnip", "display_name": "Turnip", "category": "crop",
     "max_stack": 99, "sell_price": 35, "tags": ["crop", "spring"]},
    {"id": "item.turnip_soup", "display_name": "Turnip Soup", "category": "food",
     "max_stack": 10, "sell_price": 120}
  ],
  "crops": [
    {"id": "crop.turnip", "seed_item": "item.turnip_seed", "harvest_item": "item.turnip",
     "stage_days": [1, 2, 2],
     "stage_sprites": ["tex.ground.soil", "tex.ground.grass", "tex.ground.grass"],
     "seasons": ["spring", "fall"], "regrow": false, "regrow_days": 0}
  ],
  "recipes": [
    {"id": "recipe.turnip_soup",
     "inputs": [{"item": "item.turnip", "amount": 2}],
     "output": {"item": "item.turnip_soup", "amount": 1},
     "required_minutes": 30, "machine_kind": "kitchen", "seasons": "any"}
  ],
  "enemies": [
    {"id": "enemy.slime", "max_health": 30, "damage": 5, "move_speed": 40,
     "loot_table": "loot.slime", "tags": ["slime"]}
  ],
  "loot_tables": [
    {"id": "loot.slime", "entries": [
      {"item": "item.turnip_seed", "min_amount": 1, "max_amount": 2, "chance": 0.5}
    ]}
  ]
})";

int loadCode(ContentDatabase& db, std::string_view json) {
    const fa::core::Status status = db.loadDocument(json);
    return status.isError() ? status.error().code : 0;
}

bool hasErrorCode(const ContentDatabase& db, int code) {
    for (const ContentError& error : db.errors()) {
        if (error.code == code) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("Content ids are validated and hashed", "[content][id]") {
    REQUIRE(ContentId::isValidName("item.turnip"));
    REQUIRE(ContentId::isValidName("item.turnip_seed"));
    REQUIRE(ContentId::isValidName("loot.slime"));
    REQUIRE_FALSE(ContentId::isValidName(""));
    REQUIRE_FALSE(ContentId::isValidName("item"));
    REQUIRE_FALSE(ContentId::isValidName(".item"));
    REQUIRE_FALSE(ContentId::isValidName("item."));
    REQUIRE_FALSE(ContentId::isValidName("item..turnip"));
    REQUIRE_FALSE(ContentId::isValidName("Item.Turnip"));
    REQUIRE_FALSE(ContentId::isValidName("item-turnip"));

    const ContentId a = ContentId::fromName("item.turnip");
    const ContentId b = ContentId::fromName("item.turnip");
    const ContentId c = ContentId::fromName("item.turnip_seed");
    REQUIRE(a.isValid());
    REQUIRE(a == b);
    REQUIRE(a.hash() == b.hash());
    REQUIRE(a.hash() != 0);
    REQUIRE(a.hash() != c.hash());
    REQUIRE(ContentId{}.hash() == 0);
    REQUIRE_FALSE(ContentId{}.isValid());
}

TEST_CASE("Season masks combine and parse", "[content][season]") {
    REQUIRE(fa::content::seasonMask(Season::Any) == 15);
    REQUIRE(fa::content::parseSeasonMask("spring") == fa::content::seasonMask(Season::Spring));
    REQUIRE(fa::content::parseSeasonMask("summer") == fa::content::seasonMask(Season::Summer));
    REQUIRE(fa::content::parseSeasonMask("fall") == fa::content::seasonMask(Season::Fall));
    REQUIRE(fa::content::parseSeasonMask("autumn") == fa::content::seasonMask(Season::Fall));
    REQUIRE(fa::content::parseSeasonMask("winter") == fa::content::seasonMask(Season::Winter));
    REQUIRE(fa::content::parseSeasonMask("any") == fa::content::seasonMask(Season::Any));
    REQUIRE(fa::content::parseSeasonMask("monsoon") == 0);
    REQUIRE(fa::content::toString(Season::Fall) == "fall");
    REQUIRE(fa::content::toString(Season::Any) == "any");

    const std::uint8_t springSummer = fa::content::seasonMask(Season::Spring) | fa::content::seasonMask(Season::Summer);
    REQUIRE(fa::content::seasonAllows(springSummer, Season::Spring));
    REQUIRE(fa::content::seasonAllows(springSummer, Season::Summer));
    REQUIRE_FALSE(fa::content::seasonAllows(springSummer, Season::Winter));
    REQUIRE(fa::content::seasonAllows(fa::content::seasonMask(Season::Any), Season::Winter));
    REQUIRE_FALSE(fa::content::seasonAllows(0, Season::Spring));
}

TEST_CASE("Content database loads every table and answers queries", "[content][load]") {
    ContentDatabase db;
    REQUIRE(db.loadDocument(kValidDocument).isOk());
    REQUIRE(db.errors().empty());
    REQUIRE(db.schemaVersion() == 1);
    REQUIRE(db.contentVersion() == 1);

    const fa::content::ContentStats stats = db.stats();
    REQUIRE(stats.items == 3);
    REQUIRE(stats.crops == 1);
    REQUIRE(stats.recipes == 1);
    REQUIRE(stats.enemies == 1);
    REQUIRE(stats.lootTables == 1);
    REQUIRE(stats.validationErrors == 0);
    REQUIRE(stats.schemaVersion == 1);
    REQUIRE(stats.contentVersion == 1);

    const fa::content::ItemDef* seed = db.item("item.turnip_seed");
    REQUIRE(seed != nullptr);
    REQUIRE(seed->displayName == "Turnip Seed");
    REQUIRE(seed->category == "seed");
    REQUIRE(seed->toolKind == "seed");
    REQUIRE(seed->maxStack == 99);
    REQUIRE(seed->sellPrice == 5);
    REQUIRE(seed->tags.size() == 2);
    REQUIRE_FALSE(seed->icon.isValid());
    REQUIRE(db.item("missing.item") == nullptr);
    REQUIRE(db.item("bad id") == nullptr);

    const fa::content::CropDef* crop = db.crop("crop.turnip");
    REQUIRE(crop != nullptr);
    REQUIRE(crop->seedItem == ContentId::fromName("item.turnip_seed"));
    REQUIRE(crop->harvestItem == ContentId::fromName("item.turnip"));
    REQUIRE(crop->stageDays.size() == 3);
    REQUIRE(crop->stageSprites.size() == 3);
    REQUIRE(crop->seasons == (fa::content::seasonMask(Season::Spring) | fa::content::seasonMask(Season::Fall)));
    REQUIRE_FALSE(crop->regrow);

    const fa::content::RecipeDef* recipe = db.recipe("recipe.turnip_soup");
    REQUIRE(recipe != nullptr);
    REQUIRE(recipe->inputs.size() == 1);
    REQUIRE(recipe->inputs[0].item == ContentId::fromName("item.turnip"));
    REQUIRE(recipe->inputs[0].amount == 2);
    REQUIRE(recipe->output.item == ContentId::fromName("item.turnip_soup"));
    REQUIRE(recipe->output.amount == 1);
    REQUIRE(recipe->requiredMinutes == 30);
    REQUIRE(recipe->machineKind == "kitchen");
    REQUIRE(recipe->seasons == fa::content::seasonMask(Season::Any));

    const fa::content::EnemyDef* enemy = db.enemy("enemy.slime");
    REQUIRE(enemy != nullptr);
    REQUIRE(enemy->maxHealth == 30);
    REQUIRE(enemy->damage == 5);
    REQUIRE(enemy->moveSpeed == Approx(40.0f));
    REQUIRE(enemy->lootTable == ContentId::fromName("loot.slime"));

    const fa::content::LootTableDef* loot = db.lootTable("loot.slime");
    REQUIRE(loot != nullptr);
    REQUIRE(loot->entries.size() == 1);
    REQUIRE(loot->entries[0].item == ContentId::fromName("item.turnip_seed"));
    REQUIRE(loot->entries[0].minAmount == 1);
    REQUIRE(loot->entries[0].maxAmount == 2);
    REQUIRE(loot->entries[0].chance == Approx(0.5f));

    REQUIRE(db.itemsWithTag("seed").size() == 1);
    REQUIRE(db.itemsWithTag("missing").empty());
    REQUIRE(db.itemsWithCategory("crop").size() == 1);
    REQUIRE(db.itemsWithCategory("material").empty());
    REQUIRE(db.items().size() == 3);
    REQUIRE(db.crops().size() == 1);
    REQUIRE(db.recipes().size() == 1);
    REQUIRE(db.enemies().size() == 1);
    REQUIRE(db.lootTables().size() == 1);
}

TEST_CASE("Crop seed_sprite is optional and parsed", "[content][load]") {
    constexpr std::string_view withSeed = R"({
      "schema_version": 1,
      "items": [
        {"id": "item.seed", "display_name": "Seed", "category": "seed"},
        {"id": "item.crop", "display_name": "Crop", "category": "crop"}
      ],
      "crops": [
        {"id": "crop.test", "seed_item": "item.seed", "harvest_item": "item.crop",
         "stage_days": [1], "stage_sprites": ["tex.crop.turnip_sprout"],
         "seed_sprite": "tex.crop.turnip_seed"}
      ]
    })";
    ContentDatabase content;
    REQUIRE(content.loadDocument(withSeed).isOk());
    const fa::content::CropDef* crop = content.crop(ContentId::fromName("crop.test"));
    REQUIRE(crop != nullptr);
    CHECK(crop->seedSprite.name() == "tex.crop.turnip_seed");
    REQUIRE(crop->stageSprites.size() == 1);
    CHECK(crop->stageSprites.front().name() == "tex.crop.turnip_sprout");

    constexpr std::string_view withoutSeed = R"({
      "schema_version": 1,
      "items": [
        {"id": "item.seed", "display_name": "Seed", "category": "seed"},
        {"id": "item.crop", "display_name": "Crop", "category": "crop"}
      ],
      "crops": [
        {"id": "crop.test", "seed_item": "item.seed", "harvest_item": "item.crop",
         "stage_days": [1], "stage_sprites": ["tex.crop.turnip_sprout"]}
      ]
    })";
    ContentDatabase legacy;
    REQUIRE(legacy.loadDocument(withoutSeed).isOk());
    const fa::content::CropDef* legacyCrop = legacy.crop(ContentId::fromName("crop.test"));
    REQUIRE(legacyCrop != nullptr);
    CHECK_FALSE(legacyCrop->seedSprite.isValid());

    constexpr std::string_view emptySeed = R"({
      "schema_version": 1,
      "items": [
        {"id": "item.seed", "display_name": "Seed", "category": "seed"},
        {"id": "item.crop", "display_name": "Crop", "category": "crop"}
      ],
      "crops": [
        {"id": "crop.test", "seed_item": "item.seed", "harvest_item": "item.crop",
         "stage_days": [1], "seed_sprite": ""}
      ]
    })";
    ContentDatabase broken;
    static_cast<void>(broken.loadDocument(emptySeed));
    CHECK(hasErrorCode(broken, err::kInvalidField));
}

TEST_CASE("Content database merges documents and rejects duplicate ids", "[content][load]") {
    ContentDatabase db;
    REQUIRE(db.loadDocument(R"({"schema_version": 1,
        "items": [{"id": "item.a", "display_name": "A", "category": "misc"}]})")
                .isOk());
    REQUIRE(db.contentVersion() == 1);
    REQUIRE(db.loadDocument(R"({"schema_version": 1,
        "crops": [{"id": "crop.a", "seed_item": "item.a", "harvest_item": "item.a", "stage_days": [1]}]})")
                .isOk());
    REQUIRE(db.contentVersion() == 2);
    REQUIRE(db.item("item.a") != nullptr);
    REQUIRE(db.crop("crop.a") != nullptr);

    REQUIRE(loadCode(db, R"({"schema_version": 1,
        "items": [{"id": "item.a", "display_name": "B", "category": "misc"}]})") == err::kValidationFailed);
    REQUIRE(hasErrorCode(db, err::kDuplicateId));
    REQUIRE(db.item("item.a")->displayName == "A");
    REQUIRE(db.contentVersion() == 2);
}

TEST_CASE("Content loader rejects malformed roots and schema versions", "[content][errors]") {
    ContentDatabase db;
    REQUIRE(loadCode(db, "{ not json") == err::kParseFailed);
    REQUIRE(db.errors().size() == 1);
    db.clear();
    REQUIRE(loadCode(db, "[]") == err::kInvalidField);
    db.clear();
    REQUIRE(loadCode(db, R"({"items": []})") == err::kInvalidField);
    db.clear();
    REQUIRE(loadCode(db, R"({"schema_version": 2, "items": []})") == err::kUnsupportedSchema);
    db.clear();
    REQUIRE(loadCode(db, R"({"schema_version": 1})") == 0);
    REQUIRE(db.contentVersion() == 1);
}

TEST_CASE("Content loader enforces ids and namespace prefixes", "[content][errors]") {
    ContentDatabase db;
    REQUIRE(loadCode(db, R"({"schema_version": 1,
        "items": [{"id": "turnip", "display_name": "T", "category": "misc"}]})") == err::kValidationFailed);
    REQUIRE(hasErrorCode(db, err::kInvalidId));

    db.clear();
    REQUIRE(loadCode(db, R"({"schema_version": 1,
        "items": [{"id": "crop.turnip", "display_name": "T", "category": "misc"}]})") == err::kValidationFailed);
    REQUIRE(hasErrorCode(db, err::kInvalidId));

    db.clear();
    REQUIRE(loadCode(db, R"({"schema_version": 1,
        "crops": [{"id": "crop.Turnip", "seed_item": "item.a", "harvest_item": "item.a",
                   "stage_days": [1]}]})") == err::kValidationFailed);
    REQUIRE(hasErrorCode(db, err::kInvalidId));
    REQUIRE(db.crop("crop.Turnip") == nullptr);
}

TEST_CASE("Content loader collects field and amount errors", "[content][errors]") {
    ContentDatabase db;
    REQUIRE(loadCode(db, R"({"schema_version": 1,
        "items": [{"id": "item.a", "display_name": "A", "category": "misc", "max_stack": 0}]})") ==
            err::kValidationFailed);
    REQUIRE(hasErrorCode(db, err::kInvalidAmount));

    db.clear();
    REQUIRE(loadCode(db, R"({"schema_version": 1,
        "items": [{"id": "item.a", "display_name": "A", "category": "misc",
                   "tool_kind": "laser"}]})") == err::kValidationFailed);
    REQUIRE(hasErrorCode(db, err::kInvalidField));

    db.clear();
    REQUIRE(loadCode(db, R"({"schema_version": 1,
        "items": [{"id": "item.a", "display_name": "A"}]})") == err::kValidationFailed);
    REQUIRE(hasErrorCode(db, err::kInvalidField));

    db.clear();
    REQUIRE(loadCode(db, R"({"schema_version": 1,
        "items": [{"id": "item.a", "display_name": "A", "category": "misc"}],
        "loot_tables": [{"id": "loot.a", "entries": [
            {"item": "item.a", "min_amount": 3, "max_amount": 1}]}]})") == err::kValidationFailed);
    REQUIRE(hasErrorCode(db, err::kInvalidAmount));

    db.clear();
    REQUIRE(loadCode(db, R"({"schema_version": 1,
        "items": [{"id": "item.a", "display_name": "A", "category": "misc"}],
        "loot_tables": [{"id": "loot.a", "entries": [
            {"item": "item.a", "chance": 1.5}]}]})") == err::kValidationFailed);
    REQUIRE(hasErrorCode(db, err::kInvalidAmount));

    db.clear();
    REQUIRE(loadCode(db, R"({"schema_version": 1,
        "items": [{"id": "item.a", "display_name": "A", "category": "misc"}],
        "recipes": [{"id": "recipe.a", "inputs": [], "output": {"item": "item.a"},
                     "machine_kind": "kitchen"}]})") == err::kValidationFailed);
    REQUIRE(hasErrorCode(db, err::kInvalidField));

    db.clear();
    REQUIRE(loadCode(db, R"({"schema_version": 1,
        "items": [{"id": "item.a", "display_name": "A", "category": "misc"}],
        "recipes": [{"id": "recipe.a", "inputs": [{"item": "item.a"}],
                     "output": {"item": "item.a"}, "machine_kind": ""}]})") == err::kValidationFailed);
    REQUIRE(hasErrorCode(db, err::kInvalidField));

    db.clear();
    REQUIRE(loadCode(db, R"({"schema_version": 1,
        "items": [{"id": "item.a", "display_name": "A", "category": "misc"}],
        "crops": [{"id": "crop.a", "seed_item": "item.a", "harvest_item": "item.a",
                   "stage_days": [1, 2], "stage_sprites": ["tex.a"]}]})") == err::kValidationFailed);
    REQUIRE(hasErrorCode(db, err::kInvalidField));

    db.clear();
    REQUIRE(loadCode(db, R"({"schema_version": 1,
        "items": [{"id": "item.a", "display_name": "A", "category": "misc"}],
        "crops": [{"id": "crop.a", "seed_item": "item.a", "harvest_item": "item.a",
                   "stage_days": [1], "regrow": true, "regrow_days": 0}]})") == err::kValidationFailed);
    REQUIRE(hasErrorCode(db, err::kInvalidAmount));
}

TEST_CASE("Content loader rejects invalid seasons", "[content][errors]") {
    ContentDatabase db;
    REQUIRE(loadCode(db, R"({"schema_version": 1,
        "items": [{"id": "item.a", "display_name": "A", "category": "misc"}],
        "crops": [{"id": "crop.a", "seed_item": "item.a", "harvest_item": "item.a",
                   "stage_days": [1], "seasons": "monsoon"}]})") == err::kValidationFailed);
    REQUIRE(hasErrorCode(db, err::kInvalidSeason));

    db.clear();
    REQUIRE(loadCode(db, R"({"schema_version": 1,
        "items": [{"id": "item.a", "display_name": "A", "category": "misc"}],
        "recipes": [{"id": "recipe.a", "inputs": [{"item": "item.a"}],
                     "output": {"item": "item.a"}, "machine_kind": "kitchen",
                     "seasons": ["spring", "monsoon"]}]})") == err::kValidationFailed);
    REQUIRE(hasErrorCode(db, err::kInvalidSeason));
}

TEST_CASE("Content validation reports every missing reference and can be retried", "[content][validate]") {
    ContentDatabase db;
    REQUIRE(loadCode(db, R"({
        "schema_version": 1,
        "items": [{"id": "item.a", "display_name": "A", "category": "misc"}],
        "crops": [{"id": "crop.a", "seed_item": "item.missing", "harvest_item": "item.a",
                   "stage_days": [1]}],
        "recipes": [{"id": "recipe.a", "inputs": [{"item": "item.missing"}],
                     "output": {"item": "item.a"}, "machine_kind": "kitchen"}],
        "enemies": [{"id": "enemy.a", "loot_table": "loot.missing"}],
        "loot_tables": [{"id": "loot.a", "entries": [{"item": "item.missing"}]}]
    })") == err::kValidationFailed);
    REQUIRE(hasErrorCode(db, err::kMissingReference));
    std::size_t missing = 0;
    for (const ContentError& error : db.errors()) {
        if (error.code == err::kMissingReference) {
            ++missing;
        }
    }
    REQUIRE(missing >= 4);
    REQUIRE(db.contentVersion() == 0);

    REQUIRE(db.loadDocument(R"({
        "schema_version": 1,
        "items": [{"id": "item.missing", "display_name": "M", "category": "misc"}],
        "loot_tables": [{"id": "loot.missing", "entries": []}]
    })")
                .isOk());
    REQUIRE(db.validate().isOk());
    REQUIRE(db.contentVersion() == 1);
    REQUIRE(db.errors().empty());
    REQUIRE(db.item("item.missing") != nullptr);
    REQUIRE(db.lootTable("loot.missing") != nullptr);
}

TEST_CASE("Content database clear removes definitions and errors", "[content]") {
    ContentDatabase db;
    REQUIRE(db.loadDocument(kValidDocument).isOk());
    REQUIRE_FALSE(db.items().empty());
    db.clear();
    REQUIRE(db.stats().items == 0);
    REQUIRE(db.stats().crops == 0);
    REQUIRE(db.stats().recipes == 0);
    REQUIRE(db.stats().enemies == 0);
    REQUIRE(db.stats().lootTables == 0);
    REQUIRE(db.stats().validationErrors == 0);
    REQUIRE(db.contentVersion() == 0);
    REQUIRE(db.errors().empty());
    REQUIRE(db.item("item.turnip") == nullptr);
    REQUIRE(db.crop("crop.turnip") == nullptr);
}

TEST_CASE("Item combat and food fields are parsed", "[content][items]") {
    ContentDatabase db;
    REQUIRE(db.loadDocument(R"({
        "schema_version": 1,
        "items": [
            {"id": "item.sword", "display_name": "Sword", "category": "weapon",
             "tool_kind": "sword", "damage": 12},
            {"id": "item.soup", "display_name": "Soup", "category": "food",
             "heal_amount": 20, "stamina_restore": 30}
        ]
    })")
                .isOk());

    const fa::content::ItemDef* sword = db.item("item.sword");
    REQUIRE(sword != nullptr);
    REQUIRE(sword->damage == 12);
    const fa::content::ItemDef* soup = db.item("item.soup");
    REQUIRE(soup != nullptr);
    REQUIRE(soup->healAmount == 20);
    REQUIRE(soup->staminaRestore == 30);
}

TEST_CASE("Item combat and food fields reject negatives", "[content][items]") {
    ContentDatabase db;
    REQUIRE(db.loadDocument(R"({
        "schema_version": 1,
        "items": [{"id": "item.bad", "display_name": "Bad", "category": "weapon",
                   "damage": -1}]
    })")
                .isError());
}

TEST_CASE("Content optional fields use playable defaults", "[content][defaults]") {
    ContentDatabase db;
    REQUIRE(db.loadDocument(R"({
        "schema_version": 1,
        "items": [{"id": "item.a", "display_name": "A", "category": "misc"}],
        "crops": [{"id": "crop.a", "seed_item": "item.a", "harvest_item": "item.a",
                   "stage_days": [1]}],
        "recipes": [{"id": "recipe.a", "inputs": [{"item": "item.a"}],
                     "output": {"item": "item.a"}, "machine_kind": "kitchen"}],
        "enemies": [{"id": "enemy.a"}],
        "loot_tables": [{"id": "loot.a", "entries": []}]
    })")
                .isOk());

    const fa::content::ItemDef* item = db.item("item.a");
    REQUIRE(item != nullptr);
    REQUIRE(item->maxStack == 99);
    REQUIRE(item->sellPrice == 0);
    REQUIRE(item->damage == 0);
    REQUIRE(item->healAmount == 0);
    REQUIRE(item->staminaRestore == 0);
    REQUIRE(item->tags.empty());
    REQUIRE_FALSE(item->icon.isValid());

    const fa::content::CropDef* crop = db.crop("crop.a");
    REQUIRE(crop != nullptr);
    REQUIRE(crop->stageSprites.empty());
    REQUIRE(crop->seasons == fa::content::seasonMask(Season::Any));
    REQUIRE_FALSE(crop->regrow);
    REQUIRE(crop->regrowDays == 0);

    const fa::content::RecipeDef* recipe = db.recipe("recipe.a");
    REQUIRE(recipe != nullptr);
    REQUIRE(recipe->inputs[0].amount == 1);
    REQUIRE(recipe->output.amount == 1);
    REQUIRE(recipe->requiredMinutes == 0);
    REQUIRE(recipe->seasons == fa::content::seasonMask(Season::Any));

    const fa::content::EnemyDef* enemy = db.enemy("enemy.a");
    REQUIRE(enemy != nullptr);
    REQUIRE(enemy->maxHealth == 100);
    REQUIRE(enemy->damage == 0);
    REQUIRE(enemy->moveSpeed == Approx(60.0f));
    REQUIRE_FALSE(enemy->lootTable.isValid());

    const fa::content::LootTableDef* loot = db.lootTable("loot.a");
    REQUIRE(loot != nullptr);
    REQUIRE(loot->entries.empty());
}
