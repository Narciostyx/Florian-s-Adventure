#include <fa/input/ActionMap.hpp>

#include <algorithm>
#include <utility>

#include <nlohmann/json.hpp>

#include <fa/core/Error.hpp>
#include <fa/core/Log.hpp>

namespace fa::input {
namespace {

/// Parsing uses the non-throwing overloads everywhere: the whole solution builds with
/// exceptions disabled (/EHsc- in Directory.Build.props), so a JSON_THROW would be an
/// unrecoverable path rather than something a caller could catch.
nlohmann::json parseDocument(std::string_view text, bool& ok) {
    auto document = nlohmann::json::parse(text.begin(), text.end(), nullptr, false);
    ok = !document.is_discarded();
    return document;
}

void bindFromJson(const nlohmann::json& value, ActionId action, ActionMap& out) {
    if (value.is_string()) {
        const auto binding = Binding::fromString(value.get<std::string>());
        if (isUsable(binding)) {
            out.bind(action, binding);
        }
        return;
    }
    if (!value.is_array()) {
        return;
    }
    for (const auto& element : value) {
        if (!element.is_string()) {
            continue;
        }
        const auto binding = Binding::fromString(element.get<std::string>());
        if (isUsable(binding)) {
            out.bind(action, binding);
        } else {
            fa::core::logf(fa::core::LogCategory::Input,
                           fa::core::LogLevel::Warn,
                           "input: skipping unknown binding '%s' for action %u",
                           element.get<std::string>().c_str(),
                           action);
        }
    }
}

fa::core::Status parseContext(const nlohmann::json& document, ActionMap& out, ActionRegistry& names) {
    if (!document.is_object()) {
        return fa::core::errorStatus(fa::core::error_code::kParseFailure,
                                     "input: binding document must be a JSON object");
    }
    out.clear();
    for (const auto& [key, value] : document.items()) {
        const auto action = names.intern(key);
        if (action == kInvalidActionId) {
            continue;
        }
        bindFromJson(value, action, out);
    }
    return fa::core::okStatus();
}

bool contextIndexFromName(std::string_view name, std::size_t& out) noexcept {
    for (std::size_t i = 0; i < kInputContextCount; ++i) {
        if (toString(static_cast<InputContext>(i)) == name) {
            out = i;
            return true;
        }
    }
    return false;
}

}  // namespace

void ActionMap::bind(ActionId action, Binding binding) {
    if (action == kInvalidActionId) {
        return;
    }
    auto& list = m_bindings[action];
    if (std::find(list.begin(), list.end(), binding) != list.end()) {
        return;
    }
    list.push_back(binding);
}

bool ActionMap::unbind(ActionId action, const Binding& binding) {
    const auto it = m_bindings.find(action);
    if (it == m_bindings.end()) {
        return false;
    }
    auto& list = it->second;
    const auto position = std::find(list.begin(), list.end(), binding);
    if (position == list.end()) {
        return false;
    }
    list.erase(position);
    if (list.empty()) {
        m_bindings.erase(it);
    }
    return true;
}

void ActionMap::clearAction(ActionId action) {
    m_bindings.erase(action);
}

void ActionMap::clear() noexcept {
    m_bindings.clear();
}

std::span<const Binding> ActionMap::bindings(ActionId action) const noexcept {
    const auto it = m_bindings.find(action);
    if (it == m_bindings.end()) {
        return {};
    }
    return it->second;
}

std::vector<ActionId> ActionMap::actions() const {
    std::vector<ActionId> ids;
    ids.reserve(m_bindings.size());
    for (const auto& entry : m_bindings) {
        ids.push_back(entry.first);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::string saveActionMapToJson(const ActionMap& map, const ActionRegistry& names) {
    nlohmann::json document = nlohmann::json::object();
    for (const auto action : map.actions()) {
        const auto name = names.nameView(action);
        if (name.empty()) {
            continue;
        }
        auto& list = document[std::string{name}];
        list = nlohmann::json::array();
        for (const auto& binding : map.bindings(action)) {
            auto text = binding.toString();
            if (!text.empty()) {
                list.push_back(std::move(text));
            }
        }
        if (list.empty()) {
            document.erase(std::string{name});
        }
    }
    return document.dump(2);
}

fa::core::Status loadActionMapFromJson(std::string_view json, ActionMap& out, ActionRegistry& names) {
    bool ok = false;
    const auto document = parseDocument(json, ok);
    if (!ok) {
        return fa::core::errorStatus(fa::core::error_code::kParseFailure, "input: bindings are not valid JSON");
    }
    return parseContext(document, out, names);
}

std::string saveAllActionMapsToJson(std::span<const ActionMap> maps, const ActionRegistry& names) {
    nlohmann::json root = nlohmann::json::object();
    root["version"] = 1;
    auto contexts = nlohmann::json::object();
    for (std::size_t i = 0; i < maps.size(); ++i) {
        if (maps[i].empty()) {
            continue;
        }
        bool ok = false;
        const auto parsed = parseDocument(saveActionMapToJson(maps[i], names), ok);
        if (ok) {
            contexts[std::string{toString(static_cast<InputContext>(i))}] = std::move(parsed);
        }
    }
    root["contexts"] = std::move(contexts);
    return root.dump(2);
}

fa::core::Status loadAllActionMapsFromJson(std::string_view json, std::span<ActionMap> maps, ActionRegistry& names) {
    bool ok = false;
    const auto document = parseDocument(json, ok);
    if (!ok) {
        return fa::core::errorStatus(fa::core::error_code::kParseFailure, "input: bindings are not valid JSON");
    }
    if (!document.is_object()) {
        return fa::core::errorStatus(fa::core::error_code::kParseFailure,
                                     "input: binding document must be a JSON object");
    }
    const auto contexts = document.find("contexts");
    if (contexts == document.end() || !contexts->is_object()) {
        return fa::core::errorStatus(fa::core::error_code::kParseFailure,
                                     "input: binding document has no 'contexts' object");
    }

    for (const auto& [key, value] : contexts->items()) {
        std::size_t index = 0;
        if (!contextIndexFromName(key, index)) {
            fa::core::logf(fa::core::LogCategory::Input,
                           fa::core::LogLevel::Warn,
                           "input: ignoring unknown input context '%s'",
                           key.c_str());
            continue;
        }
        if (index >= maps.size()) {
            continue;
        }
        const auto status = parseContext(value, maps[index], names);
        if (!status) {
            return status;
        }
    }
    return fa::core::okStatus();
}

}  // namespace fa::input
