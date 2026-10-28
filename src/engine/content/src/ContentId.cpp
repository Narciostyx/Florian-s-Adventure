#include "fa/content/ContentId.hpp"

#include <utility>

#include "fa/core/Hash.hpp"

namespace fa::content {

ContentId::ContentId(std::string name) : m_name(std::move(name)) {
    if (!m_name.empty()) {
        m_hash = core::fnv1a64(m_name);
    }
}

ContentId ContentId::fromName(std::string_view name) {
    return ContentId(std::string(name));
}

bool ContentId::isValidName(std::string_view name) noexcept {
    if (name.empty() || name.front() == '.' || name.back() == '.') {
        return false;
    }

    bool hasDot = false;
    bool previousWasDot = false;
    for (const char c : name) {
        if (c == '.') {
            if (previousWasDot) {
                return false;
            }
            hasDot = true;
            previousWasDot = true;
            continue;
        }
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
        previousWasDot = false;
    }
    return hasDot;
}

}  // namespace fa::content
