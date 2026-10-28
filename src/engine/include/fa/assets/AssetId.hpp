#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace fa::assets {

/// \brief Stable identifier for one asset.
///
/// The id is declared by content or code, never derived from a file path: the path
/// lives in the catalog, so moving a file does not change what gameplay refers to.
/// The 64-bit hash makes hashing and comparison cheap while the original name is
/// kept, because every log line and editor panel wants to print it.
class AssetId {
public:
    /// \brief Invalid id: no name, zero hash.
    AssetId() = default;

    /// \brief Builds an id from \p name. An empty name yields an invalid id.
    explicit AssetId(std::string name);

    /// \brief Convenience form for literals and table entries.
    static AssetId fromName(std::string_view name);

    /// \brief The declared name, e.g. `tex.ground`.
    const std::string& name() const noexcept { return m_name; }

    /// \brief FNV-1a 64 of \ref name; also the value used for hashing in maps.
    std::uint64_t hash() const noexcept { return m_hash; }

    /// \brief Whether a name was ever assigned.
    bool isValid() const noexcept { return !m_name.empty(); }

    friend bool operator==(const AssetId& lhs, const AssetId& rhs) noexcept {
        return lhs.m_hash == rhs.m_hash && lhs.m_name == rhs.m_name;
    }

    friend bool operator!=(const AssetId& lhs, const AssetId& rhs) noexcept { return !(lhs == rhs); }

    /// \brief Orders by hash, then by name. Only for sorted containers and stable
    /// test output; it carries no content meaning.
    friend bool operator<(const AssetId& lhs, const AssetId& rhs) noexcept {
        if (lhs.m_hash != rhs.m_hash) {
            return lhs.m_hash < rhs.m_hash;
        }
        return lhs.m_name < rhs.m_name;
    }

private:
    std::string m_name;
    std::uint64_t m_hash = 0;
};

}  // namespace fa::assets

namespace std {

template <>
struct hash<fa::assets::AssetId> {
    std::size_t operator()(const fa::assets::AssetId& id) const noexcept {
        return std::hash<std::uint64_t>{}(id.hash());
    }
};

}  // namespace std
