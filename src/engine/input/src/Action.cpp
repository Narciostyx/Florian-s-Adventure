#include <fa/input/Action.hpp>

namespace fa::input {

ActionId ActionRegistry::intern(std::string_view name) {
    const auto id = actionIdFromName(name);
    if (id == kInvalidActionId) {
        return kInvalidActionId;
    }
    if (m_names.find(id) == m_names.end()) {
        m_names.emplace(id, std::string(name));
    }
    return id;
}

const std::string* ActionRegistry::name(ActionId id) const noexcept {
    const auto it = m_names.find(id);
    return it == m_names.end() ? nullptr : &it->second;
}

std::string_view ActionRegistry::nameView(ActionId id) const noexcept {
    const auto* found = name(id);
    return found == nullptr ? std::string_view{} : std::string_view{*found};
}

}  // namespace fa::input
