#pragma once

#include <cstddef>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "fa/content/ContentTypes.hpp"
#include "fa/core/Result.hpp"
#include "fa/economy/EconomyTypes.hpp"
#include "fa/economy/ShopDatabase.hpp"

namespace fa::content {
class ContentDatabase;
}

namespace fa::inventory {
class Inventory;
}

namespace fa::economy {

/// \file
/// Wallet, shop transactions, processing jobs and the shipping bin.
///
/// Pure logic over borrowed content/shop tables and a borrowed inventory. Every
/// transaction checks capacity and funds before mutating anything, so a failed
/// purchase cannot take gold or partially fill the bag.

/// \brief Economy runtime for M24.
class EconomyService {
public:
    EconomyService(const content::ContentDatabase* content = nullptr, const ShopDatabase* shops = nullptr);

    void setContentDatabase(const content::ContentDatabase* content) noexcept { m_content = content; }
    void setShopDatabase(const ShopDatabase* shops) noexcept { m_shops = shops; }
    void setSeason(content::Season season) noexcept { m_season = season; }
    [[nodiscard]] content::Season season() const noexcept { return m_season; }
    /// \brief Calendar day used by the deterministic price swing.
    void setDay(int day) noexcept { m_day = day > 0 ? day : 1; }
    [[nodiscard]] int day() const noexcept { return m_day; }

    /// \brief Whether \p entry is stocked in the current season.
    [[nodiscard]] bool seasonAllows(const ShopEntry& entry) const noexcept {
        return content::seasonAllows(entry.seasons, m_season);
    }
    /// \brief Today's deterministic price factor for \p entry (`1` = list price).
    [[nodiscard]] float priceFactor(const ShopEntry& entry) const noexcept;

    void setGold(int gold) noexcept { m_gold = gold < 0 ? 0 : gold; }
    [[nodiscard]] int gold() const noexcept { return m_gold; }
    void addGold(int amount) noexcept {
        const int next = m_gold + amount;
        m_gold = next < 0 ? 0 : next;
    }

    /// \brief Unit buy price after the shop multiplier.
    [[nodiscard]] int buyPrice(const ShopDef& shop, const ShopEntry& entry, int count = 1) const noexcept;
    /// \brief Unit sell price: stock price when listed, otherwise `ItemDef::sellPrice`.
    [[nodiscard]] int sellPrice(const ShopDef& shop, const content::ContentId& item) const noexcept;

    [[nodiscard]] core::Status
    buy(const content::ContentId& shop, const content::ContentId& item, int count, inventory::Inventory& inventory);
    [[nodiscard]] core::Status
    sell(const content::ContentId& shop, const content::ContentId& item, int count, inventory::Inventory& inventory);

    /// \brief Consumes inputs and queues one processing job.
    [[nodiscard]] core::Status
    startCraft(const content::ContentId& recipe, const std::string& stationKind, inventory::Inventory& inventory);
    /// \brief Advances jobs by game minutes; emits CraftReady once per job.
    void update(int gameMinutes);
    [[nodiscard]] core::Status collectCraft(std::size_t index, inventory::Inventory& inventory);

    [[nodiscard]] core::Status ship(const content::ContentId& item, int count, inventory::Inventory& inventory);
    /// \brief Settles shipping, resets daily stock and emits events. Returns gold gained.
    int onDayChanged();

    [[nodiscard]] std::span<const ProcessingJob> jobs() const noexcept { return m_jobs; }
    /// \brief Pending shipping items, exposed for saves.
    [[nodiscard]] std::span<const inventory::ItemStack> shipping() const noexcept { return m_shipping; }
    /// \brief Replaces pending shipping items from a save snapshot.
    void restoreShipping(std::span<const inventory::ItemStack> items);
    /// \brief Replaces processing jobs from a save snapshot.
    void restoreJobs(std::span<const ProcessingJob> jobs);
    /// \brief Restores one item's remaining daily stock; unlimited items ignore it.
    void restoreRemaining(const content::ContentId& shop, const content::ContentId& item, int remaining);
    [[nodiscard]] std::size_t readyJobCount() const noexcept;
    [[nodiscard]] int remaining(const content::ContentId& shop, const content::ContentId& item) const;

    [[nodiscard]] std::span<const EconomyEvent> events() const noexcept { return m_events; }
    void clearEvents() noexcept { m_events.clear(); }
    [[nodiscard]] EconomyStats stats() const noexcept;
    void clear() noexcept;

private:
    void ensureRemaining(const ShopDef& shop);
    void resetRemaining(const ShopDef& shop);
    void record(EconomyEventKind kind,
                const content::ContentId& shop = {},
                const content::ContentId& item = {},
                const content::ContentId& recipe = {},
                int amount = 0,
                int gold = 0,
                std::string text = {});

    const content::ContentDatabase* m_content = nullptr;
    const ShopDatabase* m_shops = nullptr;
    content::Season m_season = content::Season::Spring;
    int m_day = 1;
    int m_gold = 0;
    float m_shippingMultiplier = 0.5f;
    std::map<content::ContentId, std::vector<int>> m_remaining;
    std::vector<ProcessingJob> m_jobs;
    std::vector<inventory::ItemStack> m_shipping;
    std::vector<EconomyEvent> m_events;
    EconomyStats m_totals;
};

}  // namespace fa::economy
