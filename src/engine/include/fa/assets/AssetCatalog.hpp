#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <fa/assets/AssetId.hpp>
#include <fa/assets/AssetTypes.hpp>
#include <fa/core/Math.hpp>
#include <fa/core/Result.hpp>

namespace fa::platform {
class IFileSystem;
}

namespace fa::assets {

/// \brief One catalog record: what an id means and how to fall back when its file
/// is missing.
///
/// `path` may be empty, and today usually is: an entry with no path is a pure
/// placeholder, which is how the project draws coloured blocks before real art
/// exists (docs/architecture/art-policy.md). `width`/`height` are the placeholder
/// size and are required for such entries.
struct AssetEntry {
    AssetId id;
    AssetKind kind = AssetKind::Texture;
    /// \brief File path relative to the manager's root; empty means placeholder only.
    std::string path;
    int width = 0;
    int height = 0;
    /// \brief Primary placeholder colour.
    core::Color color{1.0f, 0.0f, 1.0f, 1.0f};
    /// \brief Second placeholder colour; equal to `color` means a flat block.
    core::Color altColor{1.0f, 0.0f, 1.0f, 1.0f};
    /// \brief Checkerboard cell size in pixels; 0 or less means a flat block.
    int tileSize = 0;
};

/// \brief Id-to-file table, authored as JSON and loaded before any asset request.
///
/// Kept separate from the cache so content can be reloaded wholesale: swapping the
/// catalog is a content-pack hook, not an asset-system change.
class AssetCatalog {
public:
    /// \brief Parses a catalog document, replacing whatever this instance held.
    ///
    /// \return `asset_error::kCatalogParseFailed` with the offending entry index on a
    /// malformed document; duplicate ids are rejected the same way.
    core::Status loadFromText(std::string_view json);

    /// \brief Reads \p path through \p fileSystem, then calls loadFromText().
    core::Status loadFromFile(platform::IFileSystem& fileSystem, std::string_view path);

    void clear();

    /// \brief The entry for \p id, or null when the catalog does not declare it.
    const AssetEntry* find(const AssetId& id) const noexcept;

    /// \brief Same as find(AssetId), for call sites holding a literal name.
    const AssetEntry* find(std::string_view name) const noexcept;

    /// \brief Every entry, in document order.
    std::span<const AssetEntry> entries() const noexcept { return m_entries; }

    std::size_t size() const noexcept { return m_entries.size(); }

    bool empty() const noexcept { return m_entries.empty(); }

private:
    std::vector<AssetEntry> m_entries;
};

}  // namespace fa::assets
