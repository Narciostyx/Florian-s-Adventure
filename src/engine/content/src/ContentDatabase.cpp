#include "fa/content/ContentDatabase.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fa::content {

namespace {

using Json = nlohmann::json;

[[nodiscard]] ContentError makeError(int code, std::string context, std::string message) {
    ContentError error;
    error.code = code;
    error.context = std::move(context);
    error.message = std::move(message);
    return error;
}

[[nodiscard]] bool readIntValue(const Json& value, int& out) {
    if (value.is_number_unsigned()) {
        const unsigned long long raw = value.get<unsigned long long>();
        if (raw > static_cast<unsigned long long>(std::numeric_limits<int>::max())) {
            return false;
        }
        out = static_cast<int>(raw);
        return true;
    }
    if (value.is_number_integer()) {
        const long long raw = value.get<long long>();
        if (raw < static_cast<long long>(std::numeric_limits<int>::min()) ||
            raw > static_cast<long long>(std::numeric_limits<int>::max())) {
            return false;
        }
        out = static_cast<int>(raw);
        return true;
    }
    return false;
}

[[nodiscard]] bool readFloatValue(const Json& value, float& out) {
    if (!value.is_number()) {
        return false;
    }
    out = value.get<float>();
    return true;
}

[[nodiscard]] bool readStringValue(const Json& value, std::string& out) {
    if (!value.is_string()) {
        return false;
    }
    out = value.get<std::string>();
    return true;
}

[[nodiscard]] bool readBoolValue(const Json& value, bool& out) {
    if (!value.is_boolean()) {
        return false;
    }
    out = value.get<bool>();
    return true;
}

[[nodiscard]] bool readIntField(const Json& object, const char* key, int& out) {
    const auto found = object.find(key);
    return found != object.end() && readIntValue(*found, out);
}

[[nodiscard]] bool readFloatField(const Json& object, const char* key, float& out) {
    const auto found = object.find(key);
    return found != object.end() && readFloatValue(*found, out);
}

[[nodiscard]] bool readStringField(const Json& object, const char* key, std::string& out) {
    const auto found = object.find(key);
    return found != object.end() && readStringValue(*found, out);
}

[[nodiscard]] bool readBoolField(const Json& object, const char* key, bool& out) {
    const auto found = object.find(key);
    return found != object.end() && readBoolValue(*found, out);
}

[[nodiscard]] bool readIdField(const Json& object,
                               const char* key,
                               std::string_view prefix,
                               ContentId& out,
                               const std::string& context,
                               std::vector<ContentError>& errors) {
    std::string text;
    if (!readStringField(object, key, text)) {
        errors.push_back(
            makeError(content_error::kInvalidField, context, std::string("\"") + key + "\" must be a string"));
        return false;
    }
    if (!ContentId::isValidName(text)) {
        errors.push_back(makeError(content_error::kInvalidId, context, "invalid content id \"" + text + "\""));
        return false;
    }
    if (!text.starts_with(prefix)) {
        errors.push_back(makeError(
            content_error::kInvalidId, context, "id \"" + text + "\" must start with \"" + std::string(prefix) + "\""));
        return false;
    }
    out = ContentId::fromName(text);
    return true;
}

[[nodiscard]] bool readTags(const Json& object,
                            std::vector<std::string>& out,
                            const std::string& context,
                            std::vector<ContentError>& errors) {
    const auto found = object.find("tags");
    if (found == object.end()) {
        return true;
    }
    if (!found->is_array()) {
        errors.push_back(makeError(content_error::kInvalidField, context, "\"tags\" must be an array of strings"));
        return false;
    }
    for (const Json& value : *found) {
        std::string tag;
        if (!readStringValue(value, tag) || tag.empty()) {
            errors.push_back(
                makeError(content_error::kInvalidField, context, "\"tags\" entries must be non-empty strings"));
            return false;
        }
        out.push_back(std::move(tag));
    }
    return true;
}

[[nodiscard]] bool
readSeasons(const Json& object, std::uint8_t& out, const std::string& context, std::vector<ContentError>& errors) {
    const auto found = object.find("seasons");
    if (found == object.end()) {
        out = seasonMask(Season::Any);
        return true;
    }

    const auto parseOne = [&context, &errors](const Json& value, std::uint8_t& mask) -> bool {
        std::string name;
        if (!readStringValue(value, name)) {
            errors.push_back(makeError(content_error::kInvalidSeason, context, "season must be a string"));
            return false;
        }
        const std::uint8_t parsed = parseSeasonMask(name);
        if (parsed == 0) {
            errors.push_back(makeError(content_error::kInvalidSeason, context, "unknown season \"" + name + "\""));
            return false;
        }
        mask |= parsed;
        return true;
    };

    std::uint8_t mask = 0;
    if (found->is_string()) {
        if (!parseOne(*found, mask)) {
            return false;
        }
    } else if (found->is_array()) {
        for (const Json& value : *found) {
            if (!parseOne(value, mask)) {
                return false;
            }
        }
    } else {
        errors.push_back(
            makeError(content_error::kInvalidSeason, context, "\"seasons\" must be a string or an array of strings"));
        return false;
    }
    if (mask == 0) {
        errors.push_back(makeError(content_error::kInvalidSeason, context, "\"seasons\" is empty"));
        return false;
    }
    out = mask;
    return true;
}

[[nodiscard]] bool readAssetIdField(const Json& object,
                                    const char* key,
                                    assets::AssetId& out,
                                    const std::string& context,
                                    std::vector<ContentError>& errors) {
    const auto found = object.find(key);
    if (found == object.end()) {
        return true;
    }
    std::string name;
    if (!readStringValue(*found, name) || name.empty()) {
        errors.push_back(makeError(
            content_error::kInvalidField, context, std::string("\"") + key + "\" must be a non-empty string"));
        return false;
    }
    out = assets::AssetId::fromName(name);
    return true;
}

[[nodiscard]] bool
readItemAmount(const Json& value, ItemAmount& out, const std::string& context, std::vector<ContentError>& errors) {
    if (!value.is_object()) {
        errors.push_back(makeError(content_error::kInvalidField, context, "item amount must be an object"));
        return false;
    }
    if (!readIdField(value, "item", "item.", out.item, context, errors)) {
        return false;
    }
    out.amount = 1;
    if (value.contains("amount") && (!readIntField(value, "amount", out.amount) || out.amount <= 0)) {
        errors.push_back(makeError(content_error::kInvalidAmount, context, "\"amount\" must be a positive integer"));
        return false;
    }
    return true;
}

}  // namespace

core::Status ContentDatabase::loadDocument(std::string_view json) {
    m_parseErrors.clear();
    m_referenceErrors.clear();
    m_allErrors.clear();

    const Json document = Json::parse(json.begin(), json.end(), nullptr, false);
    if (document.is_discarded()) {
        m_parseErrors.push_back(makeError(content_error::kParseFailed, "document", "content is not valid JSON"));
        m_allErrors = m_parseErrors;
        return core::errorStatus(content_error::kParseFailed, "content is not valid JSON");
    }
    if (!document.is_object()) {
        m_parseErrors.push_back(makeError(content_error::kInvalidField, "document", "content root must be an object"));
        m_allErrors = m_parseErrors;
        return core::errorStatus(content_error::kInvalidField, "content root must be an object");
    }

    int schemaVersion = 0;
    if (!readIntField(document, "schema_version", schemaVersion)) {
        m_parseErrors.push_back(
            makeError(content_error::kInvalidField, "document", "content needs an integer \"schema_version\""));
        m_allErrors = m_parseErrors;
        return core::errorStatus(content_error::kInvalidField, "content needs an integer \"schema_version\"");
    }
    if (schemaVersion != 1) {
        m_parseErrors.push_back(makeError(content_error::kUnsupportedSchema,
                                          "document",
                                          "unsupported content schema version " + std::to_string(schemaVersion)));
        m_allErrors = m_parseErrors;
        return core::errorStatus(content_error::kUnsupportedSchema, "unsupported content schema version");
    }
    m_schemaVersion = schemaVersion;

    const auto addError = [this](int code, std::string context, std::string message) {
        m_parseErrors.push_back(makeError(code, std::move(context), std::move(message)));
    };

    if (const auto items = document.find("items"); items != document.end()) {
        if (!items->is_array()) {
            addError(content_error::kInvalidField, "items", "\"items\" must be an array");
        } else {
            std::size_t index = 0;
            for (const Json& entry : *items) {
                const std::string context = "items[" + std::to_string(index) + "]";
                ++index;
                if (!entry.is_object()) {
                    addError(content_error::kInvalidField, context, "item must be an object");
                    continue;
                }
                ItemDef def;
                if (!readIdField(entry, "id", "item.", def.id, context, m_parseErrors)) {
                    continue;
                }
                if (m_itemIndex.find(def.id) != m_itemIndex.end()) {
                    addError(content_error::kDuplicateId, context, "duplicate item id \"" + def.id.name() + "\"");
                    continue;
                }
                if (!readStringField(entry, "display_name", def.displayName) || def.displayName.empty()) {
                    addError(content_error::kInvalidField, context, "\"display_name\" must be a non-empty string");
                    continue;
                }
                if (!readStringField(entry, "category", def.category) || def.category.empty()) {
                    addError(content_error::kInvalidField, context, "\"category\" must be a non-empty string");
                    continue;
                }
                if (entry.contains("tool_kind")) {
                    if (!readStringField(entry, "tool_kind", def.toolKind) || def.toolKind.empty()) {
                        addError(content_error::kInvalidField, context, "\"tool_kind\" must be a non-empty string");
                        continue;
                    }
                    const bool known = def.toolKind == "none" || def.toolKind == "hoe" ||
                                       def.toolKind == "watering_can" || def.toolKind == "axe" ||
                                       def.toolKind == "pickaxe" || def.toolKind == "sword" || def.toolKind == "seed" ||
                                       def.toolKind == "harvest" || def.toolKind == "placeable";
                    if (!known) {
                        addError(content_error::kInvalidField,
                                 context,
                                 "\"tool_kind\" must be one of none/hoe/watering_can/axe/pickaxe/"
                                 "sword/seed/harvest/placeable");
                        continue;
                    }
                }
                if (entry.contains("description") && !readStringField(entry, "description", def.description)) {
                    addError(content_error::kInvalidField, context, "\"description\" must be a string");
                    continue;
                }
                if (!readAssetIdField(entry, "icon", def.icon, context, m_parseErrors)) {
                    continue;
                }
                if (entry.contains("max_stack")) {
                    if (!readIntField(entry, "max_stack", def.maxStack) || def.maxStack <= 0) {
                        addError(content_error::kInvalidAmount, context, "\"max_stack\" must be a positive integer");
                        continue;
                    }
                }
                if (entry.contains("sell_price")) {
                    if (!readIntField(entry, "sell_price", def.sellPrice) || def.sellPrice < 0) {
                        addError(content_error::kInvalidAmount, context, "\"sell_price\" must be >= 0");
                        continue;
                    }
                }
                if (entry.contains("damage")) {
                    if (!readIntField(entry, "damage", def.damage) || def.damage < 0) {
                        addError(content_error::kInvalidAmount, context, "\"damage\" must be >= 0");
                        continue;
                    }
                }
                if (entry.contains("heal_amount")) {
                    if (!readIntField(entry, "heal_amount", def.healAmount) || def.healAmount < 0) {
                        addError(content_error::kInvalidAmount, context, "\"heal_amount\" must be >= 0");
                        continue;
                    }
                }
                if (entry.contains("stamina_restore")) {
                    if (!readIntField(entry, "stamina_restore", def.staminaRestore) || def.staminaRestore < 0) {
                        addError(content_error::kInvalidAmount, context, "\"stamina_restore\" must be >= 0");
                        continue;
                    }
                }
                if (!readTags(entry, def.tags, context, m_parseErrors)) {
                    continue;
                }
                m_itemIndex.emplace(def.id, m_items.size());
                m_items.push_back(std::move(def));
            }
        }
    }

    if (const auto crops = document.find("crops"); crops != document.end()) {
        if (!crops->is_array()) {
            addError(content_error::kInvalidField, "crops", "\"crops\" must be an array");
        } else {
            std::size_t index = 0;
            for (const Json& entry : *crops) {
                const std::string context = "crops[" + std::to_string(index) + "]";
                ++index;
                if (!entry.is_object()) {
                    addError(content_error::kInvalidField, context, "crop must be an object");
                    continue;
                }
                CropDef def;
                if (!readIdField(entry, "id", "crop.", def.id, context, m_parseErrors)) {
                    continue;
                }
                if (m_cropIndex.find(def.id) != m_cropIndex.end()) {
                    addError(content_error::kDuplicateId, context, "duplicate crop id \"" + def.id.name() + "\"");
                    continue;
                }
                if (!readIdField(entry, "seed_item", "item.", def.seedItem, context, m_parseErrors) ||
                    !readIdField(entry, "harvest_item", "item.", def.harvestItem, context, m_parseErrors)) {
                    continue;
                }

                const auto days = entry.find("stage_days");
                if (days == entry.end() || !days->is_array() || days->empty()) {
                    addError(content_error::kInvalidField,
                             context,
                             "\"stage_days\" must be a non-empty array of positive integers");
                    continue;
                }
                bool daysOk = true;
                for (const Json& value : *days) {
                    int day = 0;
                    if (!readIntValue(value, day) || day <= 0) {
                        daysOk = false;
                        break;
                    }
                    def.stageDays.push_back(day);
                }
                if (!daysOk) {
                    addError(
                        content_error::kInvalidField, context, "\"stage_days\" must contain only positive integers");
                    continue;
                }

                if (const auto sprites = entry.find("stage_sprites"); sprites != entry.end()) {
                    if (!sprites->is_array()) {
                        addError(content_error::kInvalidField, context, "\"stage_sprites\" must be an array");
                        continue;
                    }
                    bool spritesOk = true;
                    for (const Json& value : *sprites) {
                        std::string name;
                        if (!readStringValue(value, name) || name.empty()) {
                            spritesOk = false;
                            break;
                        }
                        def.stageSprites.push_back(assets::AssetId::fromName(name));
                    }
                    if (!spritesOk) {
                        addError(
                            content_error::kInvalidField, context, "\"stage_sprites\" must contain non-empty strings");
                        continue;
                    }
                    if (def.stageSprites.size() != def.stageDays.size()) {
                        addError(content_error::kInvalidField,
                                 context,
                                 "\"stage_sprites\" must have the same length as \"stage_days\"");
                        continue;
                    }
                }

                if (const auto seedSprite = entry.find("seed_sprite"); seedSprite != entry.end()) {
                    std::string name;
                    if (!readStringValue(*seedSprite, name) || name.empty()) {
                        addError(content_error::kInvalidField, context, "\"seed_sprite\" must be a non-empty string");
                        continue;
                    }
                    def.seedSprite = assets::AssetId::fromName(name);
                }

                if (!readSeasons(entry, def.seasons, context, m_parseErrors)) {
                    continue;
                }
                if (entry.contains("regrow") && !readBoolField(entry, "regrow", def.regrow)) {
                    addError(content_error::kInvalidField, context, "\"regrow\" must be a boolean");
                    continue;
                }
                if (entry.contains("regrow_days") &&
                    (!readIntField(entry, "regrow_days", def.regrowDays) || def.regrowDays < 0)) {
                    addError(content_error::kInvalidAmount, context, "\"regrow_days\" must be >= 0");
                    continue;
                }
                if (def.regrow && def.regrowDays <= 0) {
                    addError(
                        content_error::kInvalidAmount, context, "a regrowing crop needs a positive \"regrow_days\"");
                    continue;
                }

                m_cropIndex.emplace(def.id, m_crops.size());
                m_crops.push_back(std::move(def));
            }
        }
    }

    if (const auto recipes = document.find("recipes"); recipes != document.end()) {
        if (!recipes->is_array()) {
            addError(content_error::kInvalidField, "recipes", "\"recipes\" must be an array");
        } else {
            std::size_t index = 0;
            for (const Json& entry : *recipes) {
                const std::string context = "recipes[" + std::to_string(index) + "]";
                ++index;
                if (!entry.is_object()) {
                    addError(content_error::kInvalidField, context, "recipe must be an object");
                    continue;
                }
                RecipeDef def;
                if (!readIdField(entry, "id", "recipe.", def.id, context, m_parseErrors)) {
                    continue;
                }
                if (m_recipeIndex.find(def.id) != m_recipeIndex.end()) {
                    addError(content_error::kDuplicateId, context, "duplicate recipe id \"" + def.id.name() + "\"");
                    continue;
                }

                const auto inputs = entry.find("inputs");
                if (inputs == entry.end() || !inputs->is_array() || inputs->empty()) {
                    addError(content_error::kInvalidField, context, "\"inputs\" must be a non-empty array");
                    continue;
                }
                bool inputsOk = true;
                std::size_t inputIndex = 0;
                for (const Json& value : *inputs) {
                    ItemAmount amount;
                    if (!readItemAmount(
                            value, amount, context + ".inputs[" + std::to_string(inputIndex) + "]", m_parseErrors)) {
                        inputsOk = false;
                        break;
                    }
                    def.inputs.push_back(std::move(amount));
                    ++inputIndex;
                }
                if (!inputsOk) {
                    continue;
                }

                const auto output = entry.find("output");
                if (output == entry.end() || !readItemAmount(*output, def.output, context + ".output", m_parseErrors)) {
                    addError(content_error::kInvalidField, context, "\"output\" must be an item amount");
                    continue;
                }
                if (entry.contains("required_minutes") &&
                    (!readIntField(entry, "required_minutes", def.requiredMinutes) || def.requiredMinutes < 0)) {
                    addError(content_error::kInvalidAmount, context, "\"required_minutes\" must be >= 0");
                    continue;
                }
                if (!readStringField(entry, "machine_kind", def.machineKind) || def.machineKind.empty()) {
                    addError(content_error::kInvalidField, context, "\"machine_kind\" must be a non-empty string");
                    continue;
                }
                if (!readSeasons(entry, def.seasons, context, m_parseErrors)) {
                    continue;
                }

                m_recipeIndex.emplace(def.id, m_recipes.size());
                m_recipes.push_back(std::move(def));
            }
        }
    }

    if (const auto enemies = document.find("enemies"); enemies != document.end()) {
        if (!enemies->is_array()) {
            addError(content_error::kInvalidField, "enemies", "\"enemies\" must be an array");
        } else {
            std::size_t index = 0;
            for (const Json& entry : *enemies) {
                const std::string context = "enemies[" + std::to_string(index) + "]";
                ++index;
                if (!entry.is_object()) {
                    addError(content_error::kInvalidField, context, "enemy must be an object");
                    continue;
                }
                EnemyDef def;
                if (!readIdField(entry, "id", "enemy.", def.id, context, m_parseErrors)) {
                    continue;
                }
                if (m_enemyIndex.find(def.id) != m_enemyIndex.end()) {
                    addError(content_error::kDuplicateId, context, "duplicate enemy id \"" + def.id.name() + "\"");
                    continue;
                }
                if (entry.contains("max_health") &&
                    (!readIntField(entry, "max_health", def.maxHealth) || def.maxHealth <= 0)) {
                    addError(content_error::kInvalidAmount, context, "\"max_health\" must be a positive integer");
                    continue;
                }
                if (entry.contains("damage") && (!readIntField(entry, "damage", def.damage) || def.damage < 0)) {
                    addError(content_error::kInvalidAmount, context, "\"damage\" must be >= 0");
                    continue;
                }
                if (entry.contains("move_speed") &&
                    (!readFloatField(entry, "move_speed", def.moveSpeed) || def.moveSpeed <= 0.0f)) {
                    addError(content_error::kInvalidAmount, context, "\"move_speed\" must be > 0");
                    continue;
                }
                if (const auto loot = entry.find("loot_table"); loot != entry.end()) {
                    if (!readIdField(entry, "loot_table", "loot.", def.lootTable, context, m_parseErrors)) {
                        continue;
                    }
                }
                if (!readTags(entry, def.tags, context, m_parseErrors)) {
                    continue;
                }

                m_enemyIndex.emplace(def.id, m_enemies.size());
                m_enemies.push_back(std::move(def));
            }
        }
    }

    if (const auto lootTables = document.find("loot_tables"); lootTables != document.end()) {
        if (!lootTables->is_array()) {
            addError(content_error::kInvalidField, "loot_tables", "\"loot_tables\" must be an array");
        } else {
            std::size_t index = 0;
            for (const Json& entry : *lootTables) {
                const std::string context = "loot_tables[" + std::to_string(index) + "]";
                ++index;
                if (!entry.is_object()) {
                    addError(content_error::kInvalidField, context, "loot table must be an object");
                    continue;
                }
                LootTableDef def;
                if (!readIdField(entry, "id", "loot.", def.id, context, m_parseErrors)) {
                    continue;
                }
                if (m_lootIndex.find(def.id) != m_lootIndex.end()) {
                    addError(content_error::kDuplicateId, context, "duplicate loot table id \"" + def.id.name() + "\"");
                    continue;
                }
                const auto entries = entry.find("entries");
                if (entries == entry.end() || !entries->is_array()) {
                    addError(content_error::kInvalidField, context, "\"entries\" must be an array");
                    continue;
                }
                bool entriesOk = true;
                std::size_t entryIndex = 0;
                for (const Json& value : *entries) {
                    const std::string entryContext = context + ".entries[" + std::to_string(entryIndex) + "]";
                    ++entryIndex;
                    if (!value.is_object()) {
                        addError(content_error::kInvalidField, entryContext, "loot entry must be an object");
                        entriesOk = false;
                        break;
                    }
                    LootEntry loot;
                    if (!readIdField(value, "item", "item.", loot.item, entryContext, m_parseErrors)) {
                        entriesOk = false;
                        break;
                    }
                    if (value.contains("min_amount") &&
                        (!readIntField(value, "min_amount", loot.minAmount) || loot.minAmount <= 0)) {
                        addError(content_error::kInvalidAmount, entryContext, "\"min_amount\" must be positive");
                        entriesOk = false;
                        break;
                    }
                    if (value.contains("max_amount") &&
                        (!readIntField(value, "max_amount", loot.maxAmount) || loot.maxAmount <= 0)) {
                        addError(content_error::kInvalidAmount, entryContext, "\"max_amount\" must be positive");
                        entriesOk = false;
                        break;
                    }
                    if (loot.maxAmount < loot.minAmount) {
                        addError(
                            content_error::kInvalidAmount, entryContext, "\"max_amount\" must be >= \"min_amount\"");
                        entriesOk = false;
                        break;
                    }
                    if (value.contains("chance")) {
                        if (!readFloatField(value, "chance", loot.chance) || loot.chance < 0.0f || loot.chance > 1.0f) {
                            addError(content_error::kInvalidAmount, entryContext, "\"chance\" must be in 0..1");
                            entriesOk = false;
                            break;
                        }
                    }
                    def.entries.push_back(std::move(loot));
                }
                if (!entriesOk) {
                    continue;
                }

                m_lootIndex.emplace(def.id, m_lootTables.size());
                m_lootTables.push_back(std::move(def));
            }
        }
    }

    const core::Status validation = validate();
    if (validation.isOk()) {
        ++m_contentVersion;
    }
    return validation;
}

core::Status ContentDatabase::validate() {
    m_referenceErrors.clear();

    const auto add = [this](int code, std::string context, std::string message) {
        m_referenceErrors.push_back(makeError(code, std::move(context), std::move(message)));
    };

    for (const CropDef& crop : m_crops) {
        if (m_itemIndex.find(crop.seedItem) == m_itemIndex.end()) {
            add(content_error::kMissingReference,
                crop.id.name(),
                "seed_item \"" + crop.seedItem.name() + "\" is not defined");
        }
        if (m_itemIndex.find(crop.harvestItem) == m_itemIndex.end()) {
            add(content_error::kMissingReference,
                crop.id.name(),
                "harvest_item \"" + crop.harvestItem.name() + "\" is not defined");
        }
    }

    for (const RecipeDef& recipe : m_recipes) {
        for (const ItemAmount& input : recipe.inputs) {
            if (m_itemIndex.find(input.item) == m_itemIndex.end()) {
                add(content_error::kMissingReference,
                    recipe.id.name(),
                    "input \"" + input.item.name() + "\" is not defined");
            }
        }
        if (m_itemIndex.find(recipe.output.item) == m_itemIndex.end()) {
            add(content_error::kMissingReference,
                recipe.id.name(),
                "output \"" + recipe.output.item.name() + "\" is not defined");
        }
    }

    for (const EnemyDef& enemy : m_enemies) {
        if (enemy.lootTable.isValid() && m_lootIndex.find(enemy.lootTable) == m_lootIndex.end()) {
            add(content_error::kMissingReference,
                enemy.id.name(),
                "loot_table \"" + enemy.lootTable.name() + "\" is not defined");
        }
    }

    for (const LootTableDef& table : m_lootTables) {
        for (const LootEntry& entry : table.entries) {
            if (m_itemIndex.find(entry.item) == m_itemIndex.end()) {
                add(content_error::kMissingReference,
                    table.id.name(),
                    "loot item \"" + entry.item.name() + "\" is not defined");
            }
        }
    }

    m_allErrors = m_parseErrors;
    m_allErrors.insert(m_allErrors.end(), m_referenceErrors.begin(), m_referenceErrors.end());

    if (!m_allErrors.empty()) {
        return core::errorStatus(content_error::kValidationFailed,
                                 "content has " + std::to_string(m_allErrors.size()) + " error(s)");
    }
    return core::okStatus();
}

void ContentDatabase::clear() noexcept {
    m_items.clear();
    m_crops.clear();
    m_recipes.clear();
    m_enemies.clear();
    m_lootTables.clear();
    m_itemIndex.clear();
    m_cropIndex.clear();
    m_recipeIndex.clear();
    m_enemyIndex.clear();
    m_lootIndex.clear();
    m_parseErrors.clear();
    m_referenceErrors.clear();
    m_allErrors.clear();
    m_schemaVersion = 0;
    m_contentVersion = 0;
}

const ItemDef* ContentDatabase::item(const ContentId& id) const noexcept {
    const auto found = m_itemIndex.find(id);
    return found != m_itemIndex.end() ? &m_items[found->second] : nullptr;
}

const ItemDef* ContentDatabase::item(std::string_view id) const noexcept {
    if (!ContentId::isValidName(id)) {
        return nullptr;
    }
    return item(ContentId::fromName(id));
}

const CropDef* ContentDatabase::crop(const ContentId& id) const noexcept {
    const auto found = m_cropIndex.find(id);
    return found != m_cropIndex.end() ? &m_crops[found->second] : nullptr;
}

const CropDef* ContentDatabase::crop(std::string_view id) const noexcept {
    if (!ContentId::isValidName(id)) {
        return nullptr;
    }
    return crop(ContentId::fromName(id));
}

const RecipeDef* ContentDatabase::recipe(const ContentId& id) const noexcept {
    const auto found = m_recipeIndex.find(id);
    return found != m_recipeIndex.end() ? &m_recipes[found->second] : nullptr;
}

const RecipeDef* ContentDatabase::recipe(std::string_view id) const noexcept {
    if (!ContentId::isValidName(id)) {
        return nullptr;
    }
    return recipe(ContentId::fromName(id));
}

const EnemyDef* ContentDatabase::enemy(const ContentId& id) const noexcept {
    const auto found = m_enemyIndex.find(id);
    return found != m_enemyIndex.end() ? &m_enemies[found->second] : nullptr;
}

const EnemyDef* ContentDatabase::enemy(std::string_view id) const noexcept {
    if (!ContentId::isValidName(id)) {
        return nullptr;
    }
    return enemy(ContentId::fromName(id));
}

const LootTableDef* ContentDatabase::lootTable(const ContentId& id) const noexcept {
    const auto found = m_lootIndex.find(id);
    return found != m_lootIndex.end() ? &m_lootTables[found->second] : nullptr;
}

const LootTableDef* ContentDatabase::lootTable(std::string_view id) const noexcept {
    if (!ContentId::isValidName(id)) {
        return nullptr;
    }
    return lootTable(ContentId::fromName(id));
}

std::vector<const ItemDef*> ContentDatabase::itemsWithTag(std::string_view tag) const {
    std::vector<const ItemDef*> result;
    for (const ItemDef& def : m_items) {
        if (std::find(def.tags.begin(), def.tags.end(), tag) != def.tags.end()) {
            result.push_back(&def);
        }
    }
    return result;
}

std::vector<const ItemDef*> ContentDatabase::itemsWithCategory(std::string_view category) const {
    std::vector<const ItemDef*> result;
    for (const ItemDef& def : m_items) {
        if (def.category == category) {
            result.push_back(&def);
        }
    }
    return result;
}

ContentStats ContentDatabase::stats() const noexcept {
    ContentStats stats;
    stats.items = m_items.size();
    stats.crops = m_crops.size();
    stats.recipes = m_recipes.size();
    stats.enemies = m_enemies.size();
    stats.lootTables = m_lootTables.size();
    stats.validationErrors = m_allErrors.size();
    stats.schemaVersion = m_schemaVersion;
    stats.contentVersion = m_contentVersion;
    return stats;
}

}  // namespace fa::content
