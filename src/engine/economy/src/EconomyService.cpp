#include "fa/economy/EconomyService.hpp"

#include <algorithm>
#include <utility>

#include "fa/content/ContentDatabase.hpp"
#include "fa/inventory/Inventory.hpp"

namespace fa::economy {

EconomyService::EconomyService(const content::ContentDatabase* content, const ShopDatabase* shops)
    : m_content(content), m_shops(shops) {}

float EconomyService::priceFactor(const ShopEntry& entry) const noexcept {
    if (entry.priceSwing <= 0.0f) {
        return 1.0f;
    }
    std::uint64_t mixed = entry.item.hash() ^ (static_cast<std::uint64_t>(m_day) * 0x9E3779B97F4A7C15ULL);
    mixed ^= mixed >> 30;
    mixed *= 0xBF58476D1CE4E5B9ULL;
    mixed ^= mixed >> 27;
    mixed *= 0x94D049BB133111EBULL;
    mixed ^= mixed >> 31;
    const float unit = static_cast<float>((mixed >> 40) & 0xFFFFu) / 65535.0f;
    return 1.0f + entry.priceSwing * (unit * 2.0f - 1.0f);
}

int EconomyService::buyPrice(const ShopDef& shop, const ShopEntry& entry, int count) const noexcept {
    if (count <= 0) {
        return 0;
    }
    const float scaled = static_cast<float>(entry.price) * shop.buyMultiplier * priceFactor(entry);
    const int unit = std::max(1, static_cast<int>(scaled + 0.5f));
    return unit * count;
}

int EconomyService::sellPrice(const ShopDef& shop, const content::ContentId& item) const noexcept {
    int base = 0;
    for (const ShopEntry& entry : shop.stock) {
        if (entry.item == item && seasonAllows(entry)) {
            base = std::max(1, static_cast<int>(static_cast<float>(entry.price) * priceFactor(entry) + 0.5f));
            break;
        }
    }
    if (base == 0 && m_content != nullptr) {
        if (const content::ItemDef* definition = m_content->item(item); definition != nullptr) {
            base = definition->sellPrice;
        }
    }
    return std::max(0, static_cast<int>(static_cast<float>(base) * shop.sellMultiplier + 0.5f));
}

core::Status EconomyService::buy(const content::ContentId& shopId,
                                 const content::ContentId& item,
                                 int count,
                                 inventory::Inventory& inventory) {
    if (count <= 0) {
        return core::errorStatus(economy_error::kInvalidCount, "buy count must be positive");
    }
    if (m_shops == nullptr) {
        return core::errorStatus(economy_error::kNotLoaded, "shop database is not loaded");
    }
    const ShopDef* shop = m_shops->shop(shopId);
    if (shop == nullptr) {
        return core::errorStatus(economy_error::kUnknownShop, "unknown shop");
    }
    const auto entry = std::find_if(
        shop->stock.begin(), shop->stock.end(), [&item](const ShopEntry& value) { return value.item == item; });
    if (entry == shop->stock.end()) {
        return core::errorStatus(economy_error::kUnknownItem, "shop does not stock this item");
    }
    if (!seasonAllows(*entry)) {
        return core::errorStatus(economy_error::kWrongSeason, "item is not stocked in this season");
    }

    ensureRemaining(*shop);
    const std::size_t index = static_cast<std::size_t>(std::distance(shop->stock.begin(), entry));
    std::vector<int>& remaining = m_remaining[shopId];
    if (entry->dailyLimit >= 0 && remaining[index] < count) {
        return core::errorStatus(economy_error::kDailyLimit, "daily stock is exhausted");
    }

    const int total = buyPrice(*shop, *entry, count);
    if (m_gold < total) {
        return core::errorStatus(economy_error::kInsufficientGold, "not enough gold");
    }
    const inventory::ItemStack stack{item, count, -1};
    if (inventory.spaceFor(stack) < count) {
        return core::errorStatus(economy_error::kInventoryFull, "inventory cannot hold the purchase");
    }
    const core::Result<int> added = inventory.add(stack);
    if (added.isError()) {
        return core::errorStatus(added.error().code, added.error().message);
    }
    if (added.value() != 0) {
        return core::errorStatus(economy_error::kInventoryFull, "inventory cannot hold the purchase");
    }

    m_gold -= total;
    if (entry->dailyLimit >= 0) {
        remaining[index] -= count;
    }
    ++m_totals.bought;
    m_totals.goldSpent += total;
    record(EconomyEventKind::ItemBought, shopId, item, {}, count, total);
    return core::okStatus();
}

core::Status EconomyService::sell(const content::ContentId& shopId,
                                  const content::ContentId& item,
                                  int count,
                                  inventory::Inventory& inventory) {
    if (count <= 0) {
        return core::errorStatus(economy_error::kInvalidCount, "sell count must be positive");
    }
    if (m_shops == nullptr) {
        return core::errorStatus(economy_error::kNotLoaded, "shop database is not loaded");
    }
    const ShopDef* shop = m_shops->shop(shopId);
    if (shop == nullptr) {
        return core::errorStatus(economy_error::kUnknownShop, "unknown shop");
    }
    if (inventory.countOf(item) < count) {
        return core::errorStatus(economy_error::kInsufficientItems, "inventory does not hold the items");
    }
    const int unit = sellPrice(*shop, item);
    if (unit <= 0) {
        return core::errorStatus(economy_error::kUnknownItem, "item has no sell price");
    }
    const core::Result<inventory::ItemStack> removed = inventory.remove(item, count);
    if (removed.isError()) {
        return core::errorStatus(removed.error().code, removed.error().message);
    }
    const int total = unit * count;
    m_gold += total;
    ++m_totals.sold;
    m_totals.goldEarned += total;
    record(EconomyEventKind::ItemSold, shopId, item, {}, count, total);
    return core::okStatus();
}

core::Status EconomyService::startCraft(const content::ContentId& recipeId,
                                        const std::string& stationKind,
                                        inventory::Inventory& inventory) {
    if (m_content == nullptr) {
        return core::errorStatus(economy_error::kNotLoaded, "content database is not loaded");
    }
    const content::RecipeDef* recipe = m_content->recipe(recipeId);
    if (recipe == nullptr) {
        return core::errorStatus(economy_error::kUnknownRecipe, "unknown recipe");
    }
    if (!content::seasonAllows(recipe->seasons, m_season)) {
        return core::errorStatus(economy_error::kWrongSeason, "recipe is not available this season");
    }
    if (!recipe->machineKind.empty() && recipe->machineKind != stationKind) {
        return core::errorStatus(economy_error::kWrongStation, "wrong crafting station");
    }
    for (const content::ItemAmount& input : recipe->inputs) {
        if (inventory.countOf(input.item) < input.amount) {
            return core::errorStatus(economy_error::kInsufficientItems, "missing crafting input");
        }
    }

    std::vector<inventory::ItemStack> removed;
    removed.reserve(recipe->inputs.size());
    for (const content::ItemAmount& input : recipe->inputs) {
        const core::Result<inventory::ItemStack> taken = inventory.remove(input.item, input.amount);
        if (taken.isError()) {
            for (const inventory::ItemStack& restore : removed) {
                static_cast<void>(inventory.add(restore));
            }
            return core::errorStatus(taken.error().code, taken.error().message);
        }
        removed.push_back(taken.value());
    }

    ProcessingJob job;
    job.recipe = recipe->id;
    job.output = recipe->output.item;
    job.outputCount = recipe->output.amount;
    job.machineKind = recipe->machineKind;
    job.remainingMinutes = std::max(0, recipe->requiredMinutes);
    job.ready = job.remainingMinutes == 0;
    m_jobs.push_back(std::move(job));
    ++m_totals.crafted;
    record(EconomyEventKind::CraftStarted, {}, recipe->output.item, recipe->id, recipe->output.amount, 0);
    if (m_jobs.back().ready) {
        record(EconomyEventKind::CraftReady, {}, recipe->output.item, recipe->id, recipe->output.amount, 0);
    }
    return core::okStatus();
}

void EconomyService::update(int gameMinutes) {
    if (gameMinutes <= 0) {
        return;
    }
    for (ProcessingJob& job : m_jobs) {
        if (job.ready) {
            continue;
        }
        job.remainingMinutes -= gameMinutes;
        if (job.remainingMinutes <= 0) {
            job.remainingMinutes = 0;
            job.ready = true;
            record(EconomyEventKind::CraftReady, {}, job.output, job.recipe, job.outputCount, 0);
        }
    }
}

core::Status EconomyService::collectCraft(std::size_t index, inventory::Inventory& inventory) {
    if (index >= m_jobs.size()) {
        return core::errorStatus(economy_error::kInvalidJob, "job index is out of range");
    }
    ProcessingJob& job = m_jobs[index];
    if (!job.ready) {
        return core::errorStatus(economy_error::kJobNotReady, "job is still processing");
    }
    const inventory::ItemStack output{job.output, job.outputCount, -1};
    if (inventory.spaceFor(output) < output.count) {
        return core::errorStatus(economy_error::kInventoryFull, "inventory cannot hold the output");
    }
    const core::Result<int> added = inventory.add(output);
    if (added.isError()) {
        return core::errorStatus(added.error().code, added.error().message);
    }
    if (added.value() != 0) {
        return core::errorStatus(economy_error::kInventoryFull, "inventory cannot hold the output");
    }
    record(EconomyEventKind::CraftCollected, {}, job.output, job.recipe, job.outputCount, 0);
    m_jobs.erase(m_jobs.begin() + static_cast<std::ptrdiff_t>(index));
    return core::okStatus();
}

core::Status EconomyService::ship(const content::ContentId& item, int count, inventory::Inventory& inventory) {
    if (count <= 0) {
        return core::errorStatus(economy_error::kInvalidCount, "ship count must be positive");
    }
    if (inventory.countOf(item) < count) {
        return core::errorStatus(economy_error::kInsufficientItems, "inventory does not hold the items");
    }
    const core::Result<inventory::ItemStack> removed = inventory.remove(item, count);
    if (removed.isError()) {
        return core::errorStatus(removed.error().code, removed.error().message);
    }

    const auto existing = std::find_if(
        m_shipping.begin(), m_shipping.end(), [&item](const inventory::ItemStack& stack) { return stack.id == item; });
    if (existing != m_shipping.end()) {
        existing->count += count;
    } else {
        m_shipping.push_back(inventory::ItemStack{item, count, -1});
    }
    m_totals.shipped += static_cast<std::uint64_t>(count);
    record(EconomyEventKind::ItemShipped, {}, item, {}, count, 0);
    return core::okStatus();
}

int EconomyService::onDayChanged() {
    if (m_shops != nullptr) {
        for (const ShopDef& shop : m_shops->shops()) {
            resetRemaining(shop);
        }
    }

    int shippingGold = 0;
    if (m_content != nullptr) {
        for (const inventory::ItemStack& stack : m_shipping) {
            if (const content::ItemDef* definition = m_content->item(stack.id); definition != nullptr) {
                shippingGold +=
                    static_cast<int>(static_cast<float>(definition->sellPrice) * m_shippingMultiplier) * stack.count;
            }
        }
    }
    if (shippingGold > 0) {
        m_gold += shippingGold;
        m_totals.goldEarned += shippingGold;
    }
    m_shipping.clear();
    record(EconomyEventKind::DailyRefresh, {}, {}, {}, 0, 0);
    if (shippingGold > 0) {
        record(EconomyEventKind::ShippingSettled, {}, {}, {}, 0, shippingGold);
    }
    return shippingGold;
}

void EconomyService::restoreShipping(std::span<const inventory::ItemStack> items) {
    m_shipping.assign(items.begin(), items.end());
}

void EconomyService::restoreJobs(std::span<const ProcessingJob> jobs) {
    m_jobs.assign(jobs.begin(), jobs.end());
}

void EconomyService::restoreRemaining(const content::ContentId& shopId, const content::ContentId& item, int remaining) {
    if (m_shops == nullptr || remaining < 0) {
        return;
    }
    const ShopDef* shop = m_shops->shop(shopId);
    if (shop == nullptr) {
        return;
    }
    const auto entry = std::find_if(
        shop->stock.begin(), shop->stock.end(), [&item](const ShopEntry& value) { return value.item == item; });
    if (entry == shop->stock.end() || entry->dailyLimit < 0) {
        return;
    }
    ensureRemaining(*shop);
    const std::size_t index = static_cast<std::size_t>(std::distance(shop->stock.begin(), entry));
    m_remaining[shopId][index] = std::min(remaining, entry->dailyLimit);
}

std::size_t EconomyService::readyJobCount() const noexcept {
    return static_cast<std::size_t>(
        std::count_if(m_jobs.begin(), m_jobs.end(), [](const ProcessingJob& job) { return job.ready; }));
}

int EconomyService::remaining(const content::ContentId& shopId, const content::ContentId& item) const {
    if (m_shops == nullptr) {
        return 0;
    }
    const ShopDef* shop = m_shops->shop(shopId);
    if (shop == nullptr) {
        return 0;
    }
    const auto entry = std::find_if(
        shop->stock.begin(), shop->stock.end(), [&item](const ShopEntry& value) { return value.item == item; });
    if (entry == shop->stock.end() || entry->dailyLimit < 0) {
        return -1;
    }
    const auto found = m_remaining.find(shopId);
    if (found == m_remaining.end()) {
        return entry->dailyLimit;
    }
    const std::size_t index = static_cast<std::size_t>(std::distance(shop->stock.begin(), entry));
    return index < found->second.size() ? found->second[index] : entry->dailyLimit;
}

EconomyStats EconomyService::stats() const noexcept {
    EconomyStats result = m_totals;
    result.gold = m_gold;
    result.shops = m_shops != nullptr ? m_shops->size() : 0;
    result.pendingJobs = m_jobs.size();
    result.readyJobs = readyJobCount();
    result.shippingItems = 0;
    for (const inventory::ItemStack& stack : m_shipping) {
        result.shippingItems += static_cast<std::size_t>(stack.count);
    }
    result.events = m_events.size();
    return result;
}

void EconomyService::clear() noexcept {
    m_gold = 0;
    m_remaining.clear();
    m_jobs.clear();
    m_shipping.clear();
    m_events.clear();
    m_totals = EconomyStats{};
}

void EconomyService::ensureRemaining(const ShopDef& shop) {
    if (m_remaining.find(shop.id) != m_remaining.end()) {
        return;
    }
    std::vector<int> remaining;
    remaining.reserve(shop.stock.size());
    for (const ShopEntry& entry : shop.stock) {
        remaining.push_back(entry.dailyLimit);
    }
    m_remaining[shop.id] = std::move(remaining);
}

void EconomyService::resetRemaining(const ShopDef& shop) {
    std::vector<int> remaining;
    remaining.reserve(shop.stock.size());
    for (const ShopEntry& entry : shop.stock) {
        remaining.push_back(entry.dailyLimit);
    }
    m_remaining[shop.id] = std::move(remaining);
}

void EconomyService::record(EconomyEventKind kind,
                            const content::ContentId& shop,
                            const content::ContentId& item,
                            const content::ContentId& recipe,
                            int amount,
                            int gold,
                            std::string text) {
    EconomyEvent event;
    event.kind = kind;
    event.shop = shop;
    event.item = item;
    event.recipe = recipe;
    event.amount = amount;
    event.gold = gold;
    event.text = std::move(text);
    m_events.push_back(std::move(event));
}

}  // namespace fa::economy
