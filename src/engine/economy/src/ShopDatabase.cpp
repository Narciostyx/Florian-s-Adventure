#include "fa/economy/ShopDatabase.hpp"

#include <map>
#include <string>

#include <nlohmann/json.hpp>

namespace fa::economy {
namespace {

using Json = nlohmann::json;

[[nodiscard]] bool hasPrefix(const content::ContentId& id, std::string_view prefix) {
    return id.name().rfind(prefix, 0) == 0;
}

[[nodiscard]] bool readStringField(const Json& object, const char* key, std::string& out) {
    const auto found = object.find(key);
    if (found == object.end() || !found->is_string()) {
        return false;
    }
    out = found->get<std::string>();
    return true;
}

[[nodiscard]] bool readIntField(const Json& object, const char* key, int& out) {
    const auto found = object.find(key);
    if (found == object.end() || !found->is_number_integer()) {
        return false;
    }
    out = found->get<int>();
    return true;
}

[[nodiscard]] bool readFloatField(const Json& object, const char* key, float& out) {
    const auto found = object.find(key);
    if (found == object.end() || !found->is_number()) {
        return false;
    }
    out = found->get<float>();
    return true;
}

[[nodiscard]] bool readIdField(const Json& object, const char* key, std::string_view prefix, content::ContentId& out) {
    std::string name;
    if (!readStringField(object, key, name)) {
        return false;
    }
    out = content::ContentId::fromName(name);
    return out.isValid() && hasPrefix(out, prefix);
}

[[nodiscard]] bool parseShop(const Json& entry, ShopDef& shop, std::string& error) {
    if (!entry.is_object() || !readIdField(entry, "id", "shop.", shop.id)) {
        error = "shop needs a valid \"id\"";
        return false;
    }
    if (entry.contains("name") && !readStringField(entry, "name", shop.name)) {
        error = "shop \"name\" must be a string";
        return false;
    }
    if (entry.contains("buy_multiplier") && !readFloatField(entry, "buy_multiplier", shop.buyMultiplier)) {
        error = "shop \"buy_multiplier\" must be a number";
        return false;
    }
    if (entry.contains("sell_multiplier") && !readFloatField(entry, "sell_multiplier", shop.sellMultiplier)) {
        error = "shop \"sell_multiplier\" must be a number";
        return false;
    }
    const auto stock = entry.find("stock");
    if (stock == entry.end() || !stock->is_array()) {
        error = "shop needs a \"stock\" array";
        return false;
    }
    for (const Json& value : *stock) {
        ShopEntry item;
        if (!value.is_object() || !readIdField(value, "item", "item.", item.item) ||
            !readIntField(value, "price", item.price) || item.price <= 0) {
            error = "shop entry needs an item id and positive price";
            return false;
        }
        if (value.contains("daily_limit") &&
            (!readIntField(value, "daily_limit", item.dailyLimit) || item.dailyLimit < -1)) {
            error = "shop entry \"daily_limit\" must be -1 or non-negative";
            return false;
        }
        if (value.contains("seasons")) {
            const auto seasons = value.find("seasons");
            if (!seasons->is_array() || seasons->empty()) {
                error = "shop entry \"seasons\" must be a non-empty array";
                return false;
            }
            std::uint8_t mask = 0;
            for (const Json& season : *seasons) {
                if (!season.is_string()) {
                    error = "shop entry season names must be strings";
                    return false;
                }
                const std::uint8_t parsed = content::parseSeasonMask(season.get<std::string>());
                if (parsed == 0) {
                    error = "shop entry has an unknown season";
                    return false;
                }
                mask |= parsed;
            }
            item.seasons = mask;
        }
        if (value.contains("price_swing")) {
            if (!readFloatField(value, "price_swing", item.priceSwing) || item.priceSwing < 0.0f ||
                item.priceSwing > 1.0f) {
                error = "shop entry \"price_swing\" must be a number in [0, 1]";
                return false;
            }
        }
        shop.stock.push_back(std::move(item));
    }
    if (shop.stock.empty()) {
        error = "shop needs at least one stock entry";
        return false;
    }
    return true;
}

}  // namespace

std::string_view toString(EconomyEventKind kind) noexcept {
    switch (kind) {
        case EconomyEventKind::ItemBought:
            return "item_bought";
        case EconomyEventKind::ItemSold:
            return "item_sold";
        case EconomyEventKind::CraftStarted:
            return "craft_started";
        case EconomyEventKind::CraftReady:
            return "craft_ready";
        case EconomyEventKind::CraftCollected:
            return "craft_collected";
        case EconomyEventKind::ItemShipped:
            return "item_shipped";
        case EconomyEventKind::ShippingSettled:
            return "shipping_settled";
        case EconomyEventKind::DailyRefresh:
            return "daily_refresh";
        default:
            return "?";
    }
}

core::Status ShopDatabase::loadDocument(std::string_view json) {
    const Json document = Json::parse(json.begin(), json.end(), nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        return core::errorStatus(economy_error::kNotLoaded, "shop document is not an object");
    }
    int schema = 0;
    if (!readIntField(document, "schema_version", schema) || schema != 1) {
        return core::errorStatus(economy_error::kNotLoaded, "shop document needs schema_version 1");
    }
    const auto values = document.find("shops");
    if (values == document.end() || !values->is_array()) {
        return core::errorStatus(economy_error::kNotLoaded, "shop document needs a \"shops\" array");
    }

    std::vector<ShopDef> parsed;
    std::map<content::ContentId, bool> ids;
    for (const Json& value : *values) {
        ShopDef shop;
        std::string error;
        if (!parseShop(value, shop, error)) {
            return core::errorStatus(economy_error::kNotLoaded, error);
        }
        if (m_index.find(shop.id) != m_index.end() || ids.find(shop.id) != ids.end()) {
            return core::errorStatus(economy_error::kNotLoaded, "duplicate shop id " + shop.id.name());
        }
        ids[shop.id] = true;
        parsed.push_back(std::move(shop));
    }

    for (ShopDef& shop : parsed) {
        m_index.emplace(shop.id, m_shops.size());
        m_shops.push_back(std::move(shop));
    }
    return core::okStatus();
}

void ShopDatabase::clear() noexcept {
    m_shops.clear();
    m_index.clear();
}

const ShopDef* ShopDatabase::shop(const content::ContentId& id) const noexcept {
    const auto found = m_index.find(id);
    return found != m_index.end() ? &m_shops[found->second] : nullptr;
}

}  // namespace fa::economy
