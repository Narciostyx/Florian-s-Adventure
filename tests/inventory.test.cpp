/// \file
/// \brief Tests for M18: item stacks, fixed-slot inventories, stacking rules,
/// add/remove/swap/split/merge/transfer, hotbar selection, equipment and item-use
/// dispatch. Everything is headless; the module never touches a renderer.
/// See docs/build/m18-inventory-items.md for the problems found while writing this.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <fa/content/ContentDatabase.hpp>
#include <fa/content/ContentId.hpp>
#include <fa/content/ContentTypes.hpp>
#include <fa/core/Result.hpp>
#include <fa/inventory/Equipment.hpp>
#include <fa/inventory/Hotbar.hpp>
#include <fa/inventory/Inventory.hpp>
#include <fa/inventory/InventoryError.hpp>
#include <fa/inventory/ItemStack.hpp>
#include <fa/inventory/ItemUseHandler.hpp>
#include <fa/inventory/ItemUseService.hpp>
#include <fa/inventory/PlayerInventory.hpp>
#include <fa/scene/EntityId.hpp>

namespace {

namespace inv_err = fa::inventory::inventory_error;

using fa::content::ContentDatabase;
using fa::content::ContentId;
using fa::content::ItemDef;
using fa::core::Status;
using fa::inventory::Equipment;
using fa::inventory::EquipmentSlot;
using fa::inventory::Hotbar;
using fa::inventory::IItemUseHandler;
using fa::inventory::Inventory;
using fa::inventory::InventoryEvent;
using fa::inventory::ItemStack;
using fa::inventory::ItemUseContext;
using fa::inventory::ItemUseEventKind;
using fa::inventory::ItemUseService;
using fa::inventory::PlayerInventory;
using fa::inventory::PlayerInventoryStats;
using fa::scene::EntityId;

constexpr std::string_view kItems = R"({
  "schema_version": 1,
  "items": [
    {"id": "item.seed", "display_name": "Seed", "category": "seed", "max_stack": 10},
    {"id": "item.crop", "display_name": "Crop", "category": "crop", "max_stack": 99},
    {"id": "item.soup", "display_name": "Soup", "category": "food", "max_stack": 5},
    {"id": "item.hoe", "display_name": "Hoe", "category": "tool", "max_stack": 1}
  ]
})";

const ContentId kSeed = ContentId::fromName("item.seed");
const ContentId kCrop = ContentId::fromName("item.crop");
const ContentId kSoup = ContentId::fromName("item.soup");
const ContentId kHoe = ContentId::fromName("item.hoe");
const ContentId kUnknown = ContentId::fromName("item.unknown");
const EntityId kUser{1u, 1u};

/// \brief Loads the item table used by every case.
struct ContentFixture {
    ContentDatabase db;
    ContentFixture() { REQUIRE(db.loadDocument(kItems).isOk()); }
};

template <typename T>
void requireError(const fa::core::Result<T>& result, int code) {
    REQUIRE(result.isError());
    CHECK(result.error().code == code);
}

int countEvents(const Inventory& inventory, InventoryEvent::Kind kind) {
    int count = 0;
    for (const InventoryEvent& event : inventory.events()) {
        if (event.kind == kind) {
            ++count;
        }
    }
    return count;
}

/// \brief Records food uses and consumes one item by default.
class FoodHandler final : public IItemUseHandler {
public:
    [[nodiscard]] bool handles(const ItemDef& item) const noexcept override { return item.category == "food"; }

    [[nodiscard]] Status use(ItemUseContext& context) override {
        ++uses;
        last = context.item.id;
        if (!consume) {
            return fa::core::okStatus();
        }
        return context.inventory.removeAt(context.slot, 1).ignoreValue();
    }

    int uses = 0;
    bool consume = true;
    ContentId last;
};

/// \brief Always fails, so the service records a failed use.
class FailingHandler final : public IItemUseHandler {
public:
    [[nodiscard]] bool handles(const ItemDef&) const noexcept override { return true; }
    [[nodiscard]] Status use(ItemUseContext&) override {
        ++uses;
        return fa::core::errorStatus(9999, "handler refused the item");
    }
    int uses = 0;
};

}  // namespace

TEST_CASE("ItemStack defaults to empty", "[inventory]") {
    ItemStack empty;
    CHECK(empty.empty());
    CHECK_FALSE(static_cast<bool>(empty));
    CHECK(empty.count == 0);
    CHECK(empty.durability == -1);

    const ItemStack crop{kCrop, 3, -1};
    CHECK_FALSE(crop.empty());
    CHECK(static_cast<bool>(crop));
    CHECK(crop == (ItemStack{kCrop, 3, -1}));
    CHECK(crop != (ItemStack{kCrop, 4, -1}));
    CHECK(crop != (ItemStack{kCrop, 3, 5}));
}

TEST_CASE_METHOD(ContentFixture, "inventory reads max stack from content", "[inventory]") {
    const Inventory inventory(3, 99, &db);
    CHECK(inventory.slotCount() == 3);
    CHECK(inventory.defaultMaxStack() == 99);
    CHECK(inventory.maxStackFor(kSeed) == 10);
    CHECK(inventory.maxStackFor(kCrop) == 99);
    CHECK(inventory.maxStackFor(kUnknown) == 99);

    const Inventory fallback(1, 7, nullptr);
    CHECK(fallback.maxStackFor(kSeed) == 7);
}

TEST_CASE_METHOD(ContentFixture, "add fills partial stacks then empty slots", "[inventory]") {
    Inventory inventory(4, 99, &db);

    const auto first = inventory.add(ItemStack{kSeed, 12, -1});
    REQUIRE(first.isOk());
    CHECK(first.value() == 0);
    CHECK(inventory.at(0).count == 10);
    CHECK(inventory.at(1).count == 2);

    const auto second = inventory.add(ItemStack{kSeed, 15, -1});
    REQUIRE(second.isOk());
    CHECK(second.value() == 0);
    CHECK(inventory.at(0).count == 10);
    CHECK(inventory.at(1).count == 10);
    CHECK(inventory.at(2).count == 7);
    CHECK(inventory.countOf(kSeed) == 27);
    CHECK(inventory.usedSlots() == 3);
    CHECK(inventory.totalItems() == 27);
    CHECK_FALSE(inventory.empty());
}

TEST_CASE_METHOD(ContentFixture, "add returns the remainder when full", "[inventory]") {
    Inventory inventory(1, 99, &db);

    const auto result = inventory.add(ItemStack{kSeed, 12, -1});
    REQUIRE(result.isOk());
    CHECK(result.value() == 2);
    CHECK(inventory.at(0).count == 10);
    CHECK(inventory.countOf(kSeed) == 10);
}

TEST_CASE_METHOD(ContentFixture, "add rejects invalid stacks", "[inventory]") {
    Inventory inventory(2, 99, &db);
    requireError(inventory.add(ItemStack{}), inv_err::kInvalidStack);
    requireError(inventory.add(ItemStack{kSeed, 0, -1}), inv_err::kInvalidQuantity);
    requireError(inventory.add(ItemStack{kSeed, -4, -1}), inv_err::kInvalidQuantity);
    CHECK(inventory.empty());
}

TEST_CASE_METHOD(ContentFixture, "add keeps different durability in separate slots", "[inventory]") {
    Inventory inventory(3, 99, &db);

    REQUIRE(inventory.add(ItemStack{kCrop, 5, 10}).isOk());
    REQUIRE(inventory.add(ItemStack{kCrop, 3, 20}).isOk());
    CHECK(inventory.at(0).count == 5);
    CHECK(inventory.at(0).durability == 10);
    CHECK(inventory.at(1).count == 3);
    CHECK(inventory.at(1).durability == 20);
    CHECK(inventory.countOf(kCrop) == 8);
}

TEST_CASE_METHOD(ContentFixture, "remove is atomic across slots", "[inventory]") {
    Inventory inventory(3, 99, &db);
    REQUIRE(inventory.add(ItemStack{kSeed, 12, -1}).isOk());

    const auto removed = inventory.remove(kSeed, 11);
    REQUIRE(removed.isOk());
    CHECK(removed.value().id == kSeed);
    CHECK(removed.value().count == 11);
    CHECK(inventory.at(0).count == 0);
    CHECK(inventory.at(1).count == 1);
    CHECK(inventory.countOf(kSeed) == 1);

    requireError(inventory.remove(kSeed, 5), inv_err::kInsufficientItems);
    CHECK(inventory.countOf(kSeed) == 1);
    requireError(inventory.remove(kUnknown, 1), inv_err::kInsufficientItems);
    requireError(inventory.remove(kSeed, 0), inv_err::kInvalidQuantity);
}

TEST_CASE_METHOD(ContentFixture, "removeAt and takeAt", "[inventory]") {
    Inventory inventory(2, 99, &db);
    REQUIRE(inventory.add(ItemStack{kCrop, 4, -1}).isOk());

    const auto two = inventory.removeAt(0, 2);
    REQUIRE(two.isOk());
    CHECK(two.value().count == 2);
    CHECK(inventory.at(0).count == 2);

    const auto rest = inventory.takeAt(0);
    REQUIRE(rest.isOk());
    CHECK(rest.value().count == 2);
    CHECK(inventory.at(0).empty());

    requireError(inventory.takeAt(0), inv_err::kSlotEmpty);
    requireError(inventory.removeAt(9, 1), inv_err::kInvalidSlot);
    requireError(inventory.removeAt(0, 0), inv_err::kInvalidQuantity);
}

TEST_CASE_METHOD(ContentFixture, "swap exchanges slots", "[inventory]") {
    Inventory inventory(3, 99, &db);
    REQUIRE(inventory.setSlot(0, ItemStack{kSeed, 2, -1}).isOk());
    REQUIRE(inventory.setSlot(1, ItemStack{kCrop, 3, -1}).isOk());

    REQUIRE(inventory.swap(0, 1).isOk());
    CHECK(inventory.at(0).id == kCrop);
    CHECK(inventory.at(1).id == kSeed);
    CHECK(inventory.swap(0, 0).isOk());
    requireError(inventory.swap(-1, 0), inv_err::kInvalidSlot);
    CHECK(countEvents(inventory, InventoryEvent::Kind::Swapped) == 1);
}

TEST_CASE_METHOD(ContentFixture, "split moves part of a stack to an empty slot", "[inventory]") {
    Inventory inventory(3, 99, &db);
    REQUIRE(inventory.add(ItemStack{kSeed, 12, -1}).isOk());

    const auto target = inventory.split(0, 4);
    REQUIRE(target.isOk());
    CHECK(target.value() == 2);
    CHECK(inventory.at(0).count == 6);
    CHECK(inventory.at(2).count == 4);
    CHECK(inventory.at(2).id == kSeed);

    requireError(inventory.split(0, 0), inv_err::kInvalidQuantity);
    requireError(inventory.split(0, 6), inv_err::kInvalidQuantity);
    requireError(inventory.split(1, 1), inv_err::kInventoryFull);

    Inventory sparse(2, 99, &db);
    requireError(sparse.split(0, 1), inv_err::kSlotEmpty);

    Inventory full(1, 99, &db);
    REQUIRE(full.add(ItemStack{kSeed, 5, -1}).isOk());
    requireError(full.split(0, 2), inv_err::kInventoryFull);
}

TEST_CASE_METHOD(ContentFixture, "merge combines matching stacks", "[inventory]") {
    Inventory inventory(3, 99, &db);
    REQUIRE(inventory.setSlot(0, ItemStack{kSeed, 4, -1}).isOk());
    REQUIRE(inventory.setSlot(1, ItemStack{kSeed, 2, -1}).isOk());

    const auto remaining = inventory.merge(0, 1);
    REQUIRE(remaining.isOk());
    CHECK(remaining.value() == 0);
    CHECK(inventory.at(0).empty());
    CHECK(inventory.at(1).count == 6);

    REQUIRE(inventory.setSlot(0, ItemStack{kSeed, 4, -1}).isOk());
    REQUIRE(inventory.setSlot(1, ItemStack{kSeed, 10, -1}).isOk());
    requireError(inventory.merge(0, 1), inv_err::kStackLimitExceeded);

    REQUIRE(inventory.setSlot(2, ItemStack{kCrop, 2, -1}).isOk());
    requireError(inventory.merge(0, 2), inv_err::kItemMismatch);
    requireError(inventory.merge(0, 0), inv_err::kInvalidTransfer);
}

TEST_CASE_METHOD(ContentFixture, "transferTo respects destination capacity", "[inventory]") {
    Inventory from(2, 99, &db);
    Inventory to(2, 99, &db);
    REQUIRE(from.add(ItemStack{kSeed, 12, -1}).isOk());
    REQUIRE(to.add(ItemStack{kSeed, 7, -1}).isOk());

    const auto first = from.transferTo(to, 0, 5);
    REQUIRE(first.isOk());
    CHECK(first.value() == 5);
    CHECK(from.at(0).count == 5);
    CHECK(to.at(0).count == 10);

    const auto second = from.transferTo(to, 0, 5);
    REQUIRE(second.isOk());
    CHECK(second.value() == 5);
    CHECK(from.at(0).empty());
    CHECK(to.at(1).count == 7);

    requireError(from.transferTo(from, 0, 1), inv_err::kInvalidTransfer);
    requireError(from.transferTo(to, 5, 1), inv_err::kInvalidSlot);
    requireError(from.transferTo(to, 0, 0), inv_err::kInvalidQuantity);
    requireError(from.transferTo(to, 0, 1), inv_err::kSlotEmpty);
}

TEST_CASE_METHOD(ContentFixture, "spaceFor reports remaining capacity", "[inventory]") {
    Inventory inventory(3, 5, nullptr);
    REQUIRE(inventory.add(ItemStack{kSeed, 7, -1}).isOk());
    CHECK(inventory.at(0).count == 5);
    CHECK(inventory.at(1).count == 2);
    CHECK(inventory.spaceFor(ItemStack{kSeed, 1, -1}) == 8);
    CHECK(inventory.spaceFor(ItemStack{kCrop, 1, -1}) == 5);
    CHECK(inventory.spaceFor(ItemStack{}) == 0);

    const auto stats = inventory.stats();
    CHECK(stats.slotCount == 3);
    CHECK(stats.usedSlots == 2);
    CHECK(stats.totalItems == 7);
    CHECK(stats.defaultMaxStack == 5);
    CHECK(stats.events > 0);
}

TEST_CASE_METHOD(ContentFixture, "setSlot and restore validate stacks", "[inventory]") {
    Inventory inventory(3, 99, &db);
    REQUIRE(inventory.setSlot(0, ItemStack{kCrop, 5, -1}).isOk());
    requireError(inventory.setSlot(0, ItemStack{kCrop, 100, -1}), inv_err::kStackLimitExceeded);
    CHECK(inventory.at(0).count == 5);
    requireError(inventory.setSlot(-1, ItemStack{kCrop, 1, -1}), inv_err::kInvalidSlot);
    requireError(inventory.setSlot(0, ItemStack{ContentId{}, 1, -1}), inv_err::kInvalidStack);
    REQUIRE(inventory.setSlot(1, ItemStack{}).isOk());
    CHECK(inventory.at(1).empty());

    const std::array<ItemStack, 2> wrongSize{ItemStack{kCrop, 1, -1}, ItemStack{}};
    requireError(inventory.restore(wrongSize), inv_err::kInvalidSlot);

    const std::array<ItemStack, 3> oversized{ItemStack{kCrop, 1, -1}, ItemStack{kCrop, 100, -1}, ItemStack{}};
    requireError(inventory.restore(oversized), inv_err::kStackLimitExceeded);
    CHECK(inventory.at(0).count == 5);

    const std::array<ItemStack, 3> snapshot{ItemStack{kSeed, 3, -1}, ItemStack{}, ItemStack{kSoup, 2, -1}};
    REQUIRE(inventory.restore(snapshot).isOk());
    CHECK(inventory.at(0).id == kSeed);
    CHECK(inventory.at(1).empty());
    CHECK(inventory.at(2).id == kSoup);
    CHECK(inventory.totalItems() == 5);
    CHECK(countEvents(inventory, InventoryEvent::Kind::Restored) >= 2);
}

TEST_CASE_METHOD(ContentFixture, "clear empties the inventory and events", "[inventory]") {
    Inventory inventory(2, 99, &db);
    REQUIRE(inventory.add(ItemStack{kCrop, 3, -1}).isOk());
    REQUIRE(inventory.events().size() > 0);
    inventory.clear();
    CHECK(inventory.empty());
    CHECK(inventory.events().empty());
}

TEST_CASE_METHOD(ContentFixture, "hotbar selects, cycles and consumes", "[inventory]") {
    Hotbar hotbar(5, 99, &db);
    CHECK(hotbar.selected() == 0);
    CHECK(hotbar.selectedStack().empty());

    CHECK(hotbar.select(2));
    CHECK(hotbar.selected() == 2);
    CHECK_FALSE(hotbar.select(9));
    CHECK(hotbar.selected() == 2);

    CHECK(hotbar.cycle(1) == 3);
    CHECK(hotbar.cycle(-1) == 2);
    CHECK(hotbar.cycle(-5) == 2);
    CHECK(hotbar.cycle(3) == 0);
    CHECK(hotbar.events().size() >= 4);

    REQUIRE(hotbar.inventory().setSlot(0, ItemStack{kSoup, 3, -1}).isOk());
    const auto consumed = hotbar.consumeSelected(2);
    REQUIRE(consumed.isOk());
    CHECK(consumed.value().count == 2);
    CHECK(hotbar.selectedStack().count == 1);
    requireError(hotbar.consumeSelected(2), inv_err::kInsufficientItems);
}

TEST_CASE_METHOD(ContentFixture, "equipment equips and unequips single items", "[inventory]") {
    Equipment equipment(&db);
    CHECK(equipment.empty(EquipmentSlot::Tool));
    CHECK_FALSE(equipment.stats().tool);

    const auto first = equipment.equip(EquipmentSlot::Tool, ItemStack{kHoe, 1, -1});
    REQUIRE(first.isOk());
    CHECK(first.value().empty());
    CHECK(equipment.at(EquipmentSlot::Tool).id == kHoe);
    CHECK(equipment.stats().tool);

    const auto replaced = equipment.equip(EquipmentSlot::Tool, ItemStack{kHoe, 1, 7});
    REQUIRE(replaced.isOk());
    CHECK(replaced.value().id == kHoe);
    CHECK(equipment.at(EquipmentSlot::Tool).durability == 7);

    requireError(equipment.equip(EquipmentSlot::Tool, ItemStack{kHoe, 2, -1}), inv_err::kInvalidQuantity);
    requireError(equipment.equip(EquipmentSlot::Tool, ItemStack{}), inv_err::kInvalidStack);
    requireError(equipment.equip(static_cast<EquipmentSlot>(9), ItemStack{kHoe, 1, -1}),
                 inv_err::kInvalidEquipmentSlot);

    const auto removed = equipment.unequip(EquipmentSlot::Tool);
    REQUIRE(removed.isOk());
    CHECK(removed.value().id == kHoe);
    CHECK(equipment.empty(EquipmentSlot::Tool));
    requireError(equipment.unequip(EquipmentSlot::Tool), inv_err::kSlotEmpty);
}

TEST_CASE_METHOD(ContentFixture, "item use reports validation failures", "[inventory]") {
    Inventory inventory(2, 99, &db);
    ItemUseService service(db);

    requireError(service.use(inventory, 0, kUser), inv_err::kSlotEmpty);
    requireError(service.use(inventory, 9, kUser), inv_err::kInvalidSlot);

    REQUIRE(inventory.setSlot(0, ItemStack{kUnknown, 1, -1}).isOk());
    requireError(service.use(inventory, 0, kUser), inv_err::kUnknownItem);

    REQUIRE(inventory.setSlot(0, ItemStack{kSoup, 1, -1}).isOk());
    requireError(service.use(inventory, 0, kUser), inv_err::kNoItemHandler);

    const auto stats = service.stats();
    CHECK(stats.handlers == 0);
    CHECK(stats.uses == 0);
    CHECK(stats.failures == 4);
    CHECK(service.events().size() == 4);
}

TEST_CASE_METHOD(ContentFixture, "item use dispatches to the first matching handler", "[inventory]") {
    Inventory inventory(2, 99, &db);
    ItemUseService service(db);
    FoodHandler food;
    service.registerHandler(food);

    REQUIRE(inventory.setSlot(0, ItemStack{kSoup, 2, -1}).isOk());
    REQUIRE(inventory.setSlot(1, ItemStack{kHoe, 1, -1}).isOk());

    REQUIRE(service.use(inventory, 0, kUser).isOk());
    CHECK(food.uses == 1);
    CHECK(food.last == kSoup);
    CHECK(inventory.at(0).count == 1);
    CHECK(service.stats().uses == 1);
    CHECK(service.stats().failures == 0);

    requireError(service.use(inventory, 1, kUser), inv_err::kNoItemHandler);
    CHECK(service.stats().failures == 1);
    CHECK(service.events().back().kind == ItemUseEventKind::Failed);
}

TEST_CASE_METHOD(ContentFixture, "item use forwards handler failures", "[inventory]") {
    Inventory inventory(1, 99, &db);
    ItemUseService service(db);
    FailingHandler failing;
    service.registerHandler(failing);

    REQUIRE(inventory.add(ItemStack{kCrop, 2, -1}).isOk());
    const auto status = service.use(inventory, 0, kUser);
    REQUIRE(status.isError());
    CHECK(status.error().code == 9999);
    CHECK(failing.uses == 1);
    CHECK(inventory.at(0).count == 2);
    CHECK(service.stats().failures == 1);
    CHECK(service.events().size() == 1);
    CHECK(service.events()[0].kind == ItemUseEventKind::Failed);
    CHECK(service.events()[0].errorCode == 9999);
}

TEST_CASE_METHOD(ContentFixture, "player inventory moves stacks between containers", "[inventory]") {
    PlayerInventory player(4, 3, &db);
    REQUIRE(player.backpack().add(ItemStack{kCrop, 5, -1}).isOk());

    const auto toHotbar = player.moveToHotbar(0, -1);
    REQUIRE(toHotbar.isOk());
    CHECK(toHotbar.value() == 5);
    CHECK(player.backpack().empty());
    CHECK(player.hotbar().inventory().at(0).count == 5);

    const auto toBackpack = player.moveToBackpack(0, 2);
    REQUIRE(toBackpack.isOk());
    CHECK(toBackpack.value() == 2);
    CHECK(player.hotbar().inventory().at(0).count == 3);
    CHECK(player.backpack().at(0).count == 2);

    requireError(player.moveToHotbar(1, -1), inv_err::kSlotEmpty);
    requireError(player.moveToBackpack(1, 1), inv_err::kSlotEmpty);
    requireError(player.moveToHotbar(9, 1), inv_err::kInvalidSlot);
    requireError(player.moveToBackpack(9, 1), inv_err::kInvalidSlot);

    const PlayerInventoryStats stats = player.stats();
    CHECK(stats.backpack.totalItems == 2);
    CHECK(stats.hotbar.totalItems == 3);
    CHECK(stats.selected == 0);
    CHECK_FALSE(stats.equipment.tool);
}

TEST_CASE_METHOD(ContentFixture, "player inventory equips from the backpack", "[inventory]") {
    PlayerInventory player(3, 2, &db);
    REQUIRE(player.backpack().setSlot(0, ItemStack{kHoe, 1, -1}).isOk());

    const auto taken = player.backpack().takeAt(0);
    REQUIRE(taken.isOk());
    REQUIRE(player.equipment().equip(EquipmentSlot::Tool, taken.value()).isOk());
    CHECK(player.backpack().empty());
    CHECK(player.equipment().at(EquipmentSlot::Tool).id == kHoe);
    CHECK(player.stats().equipment.tool);
}
