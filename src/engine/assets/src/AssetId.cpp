#include <fa/assets/AssetId.hpp>

#include <utility>

#include <fa/core/Hash.hpp>

namespace fa::assets {

AssetId::AssetId(std::string name) : m_name(std::move(name)) {
    m_hash = m_name.empty() ? 0 : fa::core::fnv1a64(m_name);
}

AssetId AssetId::fromName(std::string_view name) {
    return AssetId(std::string(name));
}

}  // namespace fa::assets
