#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <fa/assets/ImageData.hpp>

namespace fa::assets {

/// \brief What an asset id resolves to. Drives which loader the manager picks.
enum class AssetKind : std::uint8_t {
    /// \brief Pixels, optionally uploaded through `ITextureUploader`.
    Texture,
    /// \brief Raw text of a JSON document; parsing belongs to the consumer.
    JsonText,
    Count,
};

/// \brief Stable identifier name for logging and editor UI.
std::string_view toString(AssetKind kind) noexcept;

/// \brief One texture in the cache.
///
/// `gpuTexture` is an opaque token produced by `ITextureUploader`: zero means the
/// CPU pixels exist but no GPU object does, which is what a headless run sees. M06
/// turns the token into a `fa::render::TextureHandle` at the call site, which is how
/// gameplay keeps drawing with one integer-free type.
struct TextureAsset {
    ImageData image;
    std::uint32_t gpuTexture = 0;
    /// \brief Bumped by every successful (re)load, so hot-reload consumers can tell
    /// that the contents changed without comparing pixels.
    std::uint64_t revision = 0;
    /// \brief True when the pixels came from the catalog placeholder, not from a file.
    bool placeholder = false;
    /// \brief True when an uploader was registered but refused the image.
    bool uploadFailed = false;
};

/// \brief One JSON document in the cache.
///
/// The text is kept verbatim: `fa_assets` does not depend on a JSON library in its
/// public surface, so content modules parse it into their own types once and cache
/// that. `revision` follows the same rule as in `TextureAsset`.
struct JsonAsset {
    std::string text;
    std::uint64_t revision = 0;
};

/// \brief Snapshot of cache contents, filled on demand for M27's resource panel.
struct AssetStats {
    std::size_t textureCount = 0;
    std::size_t jsonCount = 0;
    std::size_t placeholderCount = 0;
    std::size_t loadingCount = 0;
    std::size_t failedCount = 0;
    /// \brief Slots whose reference count dropped to zero but which are still alive.
    std::size_t evictableCount = 0;
    /// \brief CPU bytes held by every cached image.
    std::size_t cpuBytes = 0;
    /// \brief Texture uploads the registered uploader accepted, minus releases.
    std::size_t liveGpuTextures = 0;
};

}  // namespace fa::assets
