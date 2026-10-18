#pragma once

#include <cstddef>
#include <map>
#include <span>
#include <string_view>
#include <vector>

#include "fa/core/Result.hpp"
#include "fa/economy/EconomyTypes.hpp"

namespace fa::economy {

/// \file
/// Shop documents for M24. Kept separate from M15 until the content schema grows
/// a shop table; the service only needs the parsed `ShopDef` values.

/// \brief Shop definitions and lookups.
class ShopDatabase {
public:
    ShopDatabase() = default;

    [[nodiscard]] core::Status loadDocument(std::string_view json);
    void clear() noexcept;

    [[nodiscard]] const ShopDef* shop(const content::ContentId& id) const noexcept;
    [[nodiscard]] std::span<const ShopDef> shops() const noexcept { return m_shops; }
    [[nodiscard]] std::size_t size() const noexcept { return m_shops.size(); }

private:
    std::vector<ShopDef> m_shops;
    std::map<content::ContentId, std::size_t> m_index;
};

}  // namespace fa::economy
