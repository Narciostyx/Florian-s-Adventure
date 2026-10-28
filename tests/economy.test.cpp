/// \file
/// \brief Tests for M24: shop parsing, atomic buy/sell, daily stock, processing
/// jobs, shipping settlement, prices and events.

#include <catch2/catch_test_macros.hpp>

#include <string_view>

#include "fa/content/ContentDatabase.hpp"
#include "fa/economy/EconomyService.hpp"
#include "fa/economy/ShopDatabase.hpp"
#include "fa/inventory/Inventory.hpp"

namespace {

using fa::content::ContentDatabase;
using fa::content::ContentId;
using fa::content::Season;
using fa::economy::EconomyService;
using fa::economy::ShopDatabase;
using fa::inventory::Inventory;
using fa::inventory::ItemStack;

constexpr std::string_view kContent = R"({
  "schema_version": 1,
  "items": [
    {"id": "item.seed", "display_name": "Seed", "category": "seed",
     "max_stack": 99, "sell_price": 5},
    {"id": "item.turnip", "display_name": "Turnip", "category": "crop",
     "max_stack": 99, "sell_price": 35},
    {"id": "item.soup", "display_name": "Soup", "category": "food",
     "max_stack": 10, "sell_price": 120}
  ],
  "recipes": [
    {"id": "recipe.soup",
     "inputs": [{"item": "item.turnip", "amount": 2}],
     "output": {"item": "item.soup", "amount": 1},
     "required_minutes": 30, "machine_kind": "kitchen", "seasons": ["spring"]}
  ]
})";

constexpr std::string_view kShops = R"({
  "schema_version": 1,
  "shops": [
    {"id": "shop.general", "name": "General Store",
     "buy_multiplier": 1.0, "sell_multiplier": 0.5,
     "stock": [
       {"item": "item.seed", "price": 10, "daily_limit": 5},
       {"item": "item.turnip", "price": 40}
     ]}
  ]
})";

constexpr std::string_view kSeasonalShops = R"({
  "schema_version": 1,
  "shops": [
    {"id": "shop.seasonal", "name": "Seasonal", "buy_multiplier": 1.0,
     "sell_multiplier": 0.5, "stock": [
       {"item": "item.seed", "price": 20, "daily_limit": 2,
        "seasons": ["summer"], "price_swing": 0.5},
       {"item": "item.turnip", "price": 40, "seasons": ["winter"]}
     ]}
  ]
})";

ContentDatabase makeContent() {
    ContentDatabase content;
    REQUIRE(content.loadDocument(kContent).isOk());
    return content;
}

ShopDatabase makeShops() {
    ShopDatabase shops;
    REQUIRE(shops.loadDocument(kShops).isOk());
    return shops;
}

Inventory makeInventory(const ContentDatabase& content) {
    return Inventory(8, 99, &content);
}

const ContentId kShop = ContentId::fromName("shop.general");
const ContentId kSeed = ContentId::fromName("item.seed");
const ContentId kTurnip = ContentId::fromName("item.turnip");
const ContentId kSoup = ContentId::fromName("item.soup");
const ContentId kRecipe = ContentId::fromName("recipe.soup");

}  // namespace

TEST_CASE("Shop database loads and rejects bad documents", "[economy]") {
    ShopDatabase shops;
    REQUIRE(shops.loadDocument(kShops).isOk());
    CHECK(shops.size() == 1);
    REQUIRE(shops.shop(kShop) != nullptr);
    CHECK(shops.shop(kShop)->stock.size() == 2);
    CHECK(shops.shop(ContentId::fromName("shop.none")) == nullptr);

    ShopDatabase invalid;
    CHECK(invalid.loadDocument(R"({"schema_version":1,"shops":[{"id":"bad"}]})").isError());
}

TEST_CASE("Buying checks funds, stock and inventory before mutating", "[economy]") {
    const ContentDatabase content = makeContent();
    const ShopDatabase shops = makeShops();
    Inventory inventory = makeInventory(content);
    EconomyService economy(&content, &shops);
    economy.setGold(100);

    REQUIRE(economy.buy(kShop, kSeed, 3, inventory).isOk());
    CHECK(economy.gold() == 70);
    CHECK(inventory.countOf(kSeed) == 3);
    CHECK(economy.remaining(kShop, kSeed) == 2);
    CHECK(economy.stats().bought == 1);
    CHECK(economy.stats().goldSpent == 30);

    EconomyService poor(&content, &shops);
    poor.setGold(5);
    Inventory poorInventory = makeInventory(content);
    CHECK(poor.buy(kShop, kSeed, 1, poorInventory).error().code == fa::economy::economy_error::kInsufficientGold);
    CHECK(poor.gold() == 5);
    CHECK(poorInventory.countOf(kSeed) == 0);

    Inventory full(1, 99, &content);
    REQUIRE(full.add(ItemStack{kTurnip, 1, -1}).isOk());
    CHECK(economy.buy(kShop, kSeed, 1, full).error().code == fa::economy::economy_error::kInventoryFull);
    CHECK(economy.gold() == 70);
}

TEST_CASE("Daily stock limits reset on a new day", "[economy]") {
    const ContentDatabase content = makeContent();
    const ShopDatabase shops = makeShops();
    Inventory inventory = makeInventory(content);
    EconomyService economy(&content, &shops);
    economy.setGold(1000);

    REQUIRE(economy.buy(kShop, kSeed, 5, inventory).isOk());
    CHECK(economy.remaining(kShop, kSeed) == 0);
    CHECK(economy.buy(kShop, kSeed, 1, inventory).error().code == fa::economy::economy_error::kDailyLimit);
    economy.onDayChanged();
    CHECK(economy.remaining(kShop, kSeed) == 5);
}

TEST_CASE("Selling removes items and pays the shop price", "[economy]") {
    const ContentDatabase content = makeContent();
    const ShopDatabase shops = makeShops();
    Inventory inventory = makeInventory(content);
    REQUIRE(inventory.add(ItemStack{kTurnip, 3, -1}).isOk());
    EconomyService economy(&content, &shops);

    REQUIRE(economy.sell(kShop, kTurnip, 2, inventory).isOk());
    CHECK(inventory.countOf(kTurnip) == 1);
    CHECK(economy.gold() == 40);
    CHECK(economy.stats().sold == 1);
    CHECK(economy.stats().goldEarned == 40);

    CHECK(economy.sell(kShop, kSoup, 1, inventory).error().code == fa::economy::economy_error::kInsufficientItems);
}

TEST_CASE("Prices apply shop multipliers", "[economy]") {
    const ContentDatabase content = makeContent();
    const ShopDatabase shops = makeShops();
    EconomyService economy(&content, &shops);
    const fa::economy::ShopDef* shop = shops.shop(kShop);
    REQUIRE(shop != nullptr);
    const fa::economy::ShopEntry& seed = shop->stock[0];
    CHECK(economy.buyPrice(*shop, seed, 3) == 30);
    CHECK(economy.sellPrice(*shop, kTurnip) == 20);
    CHECK(economy.sellPrice(*shop, kSoup) == 60);
}

TEST_CASE("Seasonal stock and daily price swings", "[economy]") {
    const ContentDatabase content = makeContent();
    ShopDatabase shops;
    REQUIRE(shops.loadDocument(kSeasonalShops).isOk());
    Inventory inventory = makeInventory(content);
    EconomyService economy(&content, &shops);

    const auto shopId = ContentId::fromName("shop.seasonal");
    const auto* shop = shops.shop(shopId);
    REQUIRE(shop != nullptr);
    REQUIRE(shop->stock.size() == 2);
    const auto& seedEntry = shop->stock[0];

    economy.setSeason(Season::Spring);
    CHECK_FALSE(economy.seasonAllows(seedEntry));
    CHECK(economy.buy(shopId, kSeed, 1, inventory).error().code == fa::economy::economy_error::kWrongSeason);

    economy.setSeason(Season::Summer);
    CHECK(economy.seasonAllows(seedEntry));
    economy.setDay(3);
    const int day3 = economy.buyPrice(*shop, seedEntry, 1);
    CHECK(day3 >= 10);
    CHECK(day3 <= 30);
    economy.setDay(4);
    const int day4 = economy.buyPrice(*shop, seedEntry, 1);
    CHECK(day4 >= 10);
    CHECK(day4 <= 30);
    economy.setDay(3);
    CHECK(economy.buyPrice(*shop, seedEntry, 1) == day3);

    economy.setGold(1000);
    REQUIRE(economy.buy(shopId, kSeed, 1, inventory).isOk());
    CHECK(economy.remaining(shopId, kSeed) == 1);

    // Turnip is winter-only here, so spring selling falls back to the item price.
    economy.setSeason(Season::Spring);
    CHECK(economy.sellPrice(*shop, kTurnip) == 18);
}

TEST_CASE("Crafting consumes inputs, advances and collects", "[economy]") {
    const ContentDatabase content = makeContent();
    const ShopDatabase shops = makeShops();
    Inventory inventory = makeInventory(content);
    REQUIRE(inventory.add(ItemStack{kTurnip, 2, -1}).isOk());
    EconomyService economy(&content, &shops);

    REQUIRE(economy.startCraft(kRecipe, "kitchen", inventory).isOk());
    CHECK(inventory.countOf(kTurnip) == 0);
    REQUIRE(economy.jobs().size() == 1);
    CHECK_FALSE(economy.jobs().front().ready);
    CHECK(economy.collectCraft(0, inventory).error().code == fa::economy::economy_error::kJobNotReady);

    economy.update(29);
    CHECK_FALSE(economy.jobs().front().ready);
    economy.update(1);
    CHECK(economy.jobs().front().ready);
    CHECK(economy.readyJobCount() == 1);

    REQUIRE(economy.collectCraft(0, inventory).isOk());
    CHECK(inventory.countOf(kSoup) == 1);
    CHECK(economy.jobs().empty());
    CHECK(economy.stats().crafted == 1);
}

TEST_CASE("Crafting rejects missing inputs, station and season", "[economy]") {
    const ContentDatabase content = makeContent();
    const ShopDatabase shops = makeShops();
    Inventory inventory = makeInventory(content);
    EconomyService economy(&content, &shops);

    CHECK(economy.startCraft(kRecipe, "kitchen", inventory).error().code ==
          fa::economy::economy_error::kInsufficientItems);
    REQUIRE(inventory.add(ItemStack{kTurnip, 2, -1}).isOk());
    CHECK(economy.startCraft(kRecipe, "furnace", inventory).error().code == fa::economy::economy_error::kWrongStation);
    CHECK(inventory.countOf(kTurnip) == 2);
    economy.setSeason(Season::Winter);
    CHECK(economy.startCraft(kRecipe, "kitchen", inventory).error().code == fa::economy::economy_error::kWrongSeason);
}

TEST_CASE("A full inventory keeps a ready job", "[economy]") {
    const ContentDatabase content = makeContent();
    const ShopDatabase shops = makeShops();
    Inventory inventory(2, 99, &content);
    REQUIRE(inventory.add(ItemStack{kTurnip, 2, -1}).isOk());
    EconomyService economy(&content, &shops);
    REQUIRE(economy.startCraft(kRecipe, "kitchen", inventory).isOk());
    economy.update(30);
    // Both slots are empty after consuming the inputs; fill them so the output has no space.
    REQUIRE(inventory.add(ItemStack{kSeed, 1, -1}).isOk());
    REQUIRE(inventory.add(ItemStack{kTurnip, 1, -1}).isOk());
    CHECK(economy.collectCraft(0, inventory).error().code == fa::economy::economy_error::kInventoryFull);
    CHECK(economy.jobs().size() == 1);
}

TEST_CASE("Shipping settles on the next day", "[economy]") {
    const ContentDatabase content = makeContent();
    const ShopDatabase shops = makeShops();
    Inventory inventory = makeInventory(content);
    REQUIRE(inventory.add(ItemStack{kTurnip, 2, -1}).isOk());
    EconomyService economy(&content, &shops);
    economy.setGold(10);

    REQUIRE(economy.ship(kTurnip, 2, inventory).isOk());
    CHECK(inventory.countOf(kTurnip) == 0);
    CHECK(economy.stats().shippingItems == 2);
    CHECK(economy.gold() == 10);

    const int settled = economy.onDayChanged();
    CHECK(settled == 34);
    CHECK(economy.gold() == 44);
    CHECK(economy.stats().shippingItems == 0);
    CHECK(economy.stats().goldEarned == 34);
}

TEST_CASE("Economy state restores from a save snapshot", "[economy]") {
    const ContentDatabase content = makeContent();
    const ShopDatabase shops = makeShops();
    Inventory inventory = makeInventory(content);
    REQUIRE(inventory.add(ItemStack{kTurnip, 4, -1}).isOk());
    EconomyService economy(&content, &shops);
    economy.setGold(20);

    REQUIRE(economy.ship(kTurnip, 2, inventory).isOk());
    REQUIRE(economy.startCraft(kRecipe, "kitchen", inventory).isOk());
    REQUIRE(economy.buy(kShop, kSeed, 1, inventory).isOk());
    const int remaining = economy.remaining(kShop, kSeed);

    EconomyService restored(&content, &shops);
    restored.setGold(economy.gold());
    restored.restoreShipping(economy.shipping());
    restored.restoreJobs(economy.jobs());
    restored.restoreRemaining(kShop, kSeed, remaining);

    CHECK(restored.gold() == 10);
    CHECK(restored.stats().shippingItems == 2);
    REQUIRE(restored.jobs().size() == 1);
    CHECK(restored.jobs().front().recipe == kRecipe);
    CHECK(restored.jobs().front().machineKind == "kitchen");
    CHECK(restored.remaining(kShop, kSeed) == remaining);
    CHECK(restored.onDayChanged() == 34);
    CHECK(restored.gold() == 44);
    CHECK(restored.stats().shippingItems == 0);
}

TEST_CASE("Economy events and clear", "[economy]") {
    const ContentDatabase content = makeContent();
    const ShopDatabase shops = makeShops();
    Inventory inventory = makeInventory(content);
    EconomyService economy(&content, &shops);
    economy.setGold(100);
    REQUIRE(economy.buy(kShop, kSeed, 1, inventory).isOk());
    CHECK(economy.stats().events >= 1);
    CHECK(economy.stats().gold == 90);
    economy.clear();
    CHECK(economy.gold() == 0);
    CHECK(economy.events().empty());
    CHECK(economy.jobs().empty());
}
