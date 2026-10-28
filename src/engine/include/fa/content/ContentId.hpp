#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace fa::content {

/// \file
/// Namespaced content id shared by every definition table.
///
/// Content is authored as strings (`item.turnip`, `crop.turnip`) so files stay
/// readable and merge-friendly; the 64-bit hash makes map lookups cheap without
/// changing the stored spelling. IDs are stable: renaming one is a content migration,
/// not an edit.

/// \brief Stable, namespaced content identifier.
///
/// A valid name is non-empty, contains at least one dot, starts and ends with a
/// lowercase/digit/underscore segment, and uses only `a-z`, `0-9`, `_` and `.`.
/// The prefix convention (`item.`, `crop.`, `recipe.`, `enemy.`, `loot.`) is
/// enforced by the database loader, not by the type itself.
class ContentId {
public:
    ContentId() = default;

    /// \brief Builds an id from \p name. An empty name yields an invalid id.
    explicit ContentId(std::string name);

    /// \brief Convenience form for literals and table entries.
    static ContentId fromName(std::string_view name);

    /// \brief The authored spelling.
    [[nodiscard]] const std::string& name() const noexcept { return m_name; }

    /// \brief FNV-1a 64 of the name, or 0 for an invalid id.
    [[nodiscard]] std::uint64_t hash() const noexcept { return m_hash; }

    [[nodiscard]] bool isValid() const noexcept { return !m_name.empty(); }

    /// \brief Whether \p name follows the dot-separated lowercase convention.
    [[nodiscard]] static bool isValidName(std::string_view name) noexcept;

    friend bool operator==(const ContentId& lhs, const ContentId& rhs) noexcept {
        return lhs.m_hash == rhs.m_hash && lhs.m_name == rhs.m_name;
    }

    friend bool operator!=(const ContentId& lhs, const ContentId& rhs) noexcept { return !(lhs == rhs); }

    /// \brief Orders by hash, then by spelling; used by `std::map`.
    friend bool operator<(const ContentId& lhs, const ContentId& rhs) noexcept {
        if (lhs.m_hash != rhs.m_hash) {
            return lhs.m_hash < rhs.m_hash;
        }
        return lhs.m_name < rhs.m_name;
    }

private:
    std::string m_name;
    std::uint64_t m_hash = 0;
};

}  // namespace fa::content

namespace std {

template <>
struct hash<fa::content::ContentId> {
    std::size_t operator()(const fa::content::ContentId& id) const noexcept {
        return static_cast<std::size_t>(id.hash());
    }
};

}  // namespace std
