#pragma once

#include <memory>
#include <span>
#include <string>
#include <string_view>

#include <fa/assets/AssetCatalog.hpp>
#include <fa/assets/AssetHandle.hpp>
#include <fa/assets/AssetId.hpp>
#include <fa/assets/AssetTypes.hpp>
#include <fa/assets/IImageDecoder.hpp>
#include <fa/assets/ITextureUploader.hpp>
#include <fa/core/Result.hpp>

namespace fa::events {
class JobSystem;
}

namespace fa::platform {
class IFileSystem;
}

namespace fa::assets {

/// \brief How the cache is wired: where files come from, what is declared, who
/// uploads, who decodes.
///
/// Everything is a borrowed pointer: the composition root outlives the manager, and
/// a null optional dependency simply disables that feature.
struct AssetManagerDesc {
    /// \brief Required for any file-backed asset.
    platform::IFileSystem* fileSystem = nullptr;
    /// \brief Required: an id that is not declared has no size, colour or path.
    const AssetCatalog* catalog = nullptr;
    /// \brief Optional GPU bridge; when null the cache still holds CPU pixels.
    ITextureUploader* uploader = nullptr;
    /// \brief Optional pool for `requestTexture()`; when null requests load inline.
    events::JobSystem* jobs = nullptr;
    /// \brief Decoders tried in order; the first whose `supports()` says yes wins.
    std::span<IImageDecoder* const> decoders{};
    /// \brief Prefix joined with `AssetEntry::path`, e.g. `assets/`.
    std::string rootPath;
    /// \brief Keep CPU pixels after a successful upload (needed for reload and for
    /// CPU sampling, costs `width * height * bpp` per texture).
    bool retainCpuPixels = true;
    /// \brief Log every load, placeholder fallback and failure. Turn off only in
    /// tests that deliberately provoke failures.
    bool logEvents = true;
};

/// \brief The only way gameplay touches assets.
///
/// Handles returned here are valid until the manager is destroyed; a handle whose
/// slot is evicted is not, which is why `releaseAll()` exists for ordered shutdown.
class IAssetManager {
public:
    virtual ~IAssetManager() = default;

    /// \brief Loads a texture now, on the calling thread.
    ///
    /// A missing file, an unsupported extension or a failed decode falls back to the
    /// catalog placeholder and still succeeds; only an undeclared id, an entry with
    /// no usable placeholder size, or a rejected upload is an error.
    virtual core::Result<AssetHandle<TextureAsset>> loadTexture(const AssetId& id) = 0;

    /// \brief Returns a handle immediately, loading in the background when a job
    /// system is configured.
    ///
    /// The handle may be Loading; `update()` finishes it on the frame thread. Without
    /// a job system this is equivalent to loadTexture() and always ends Ready or
    /// Failed. There is no error channel, so an id that is undeclared, not a texture,
    /// or already failed yields an empty handle plus one error log line.
    virtual AssetHandle<TextureAsset> requestTexture(const AssetId& id) = 0;

    /// \brief Loads a JSON document now.
    ///
    /// There is no placeholder for JSON: a failure is reported instead, because a
    /// fabricated document would hide content bugs.
    virtual core::Result<AssetHandle<JsonAsset>> loadJson(const AssetId& id) = 0;

    /// \brief Re-reads an already cached asset, keeping every live handle pointing
    /// at the same slot and bumping its `revision`.
    virtual core::Status reload(const AssetId& id) = 0;

    /// \brief Frame-thread pump: finishes background loads, uploads pixels, evicts
    /// unreferenced slots. Call once per frame after the update phases.
    virtual void update() = 0;

    /// \brief Evicts every slot with no live handle. Safe to call any time.
    virtual void purgeUnreferenced() = 0;

    /// \brief Releases GPU objects for every cached texture. Destroys nothing else,
    /// so it is safe to call before the renderer shuts down.
    virtual void releaseGpuTextures() = 0;

    /// \brief Resets every slot this manager handed out. Handles left elsewhere become
    /// dangling, so call it while unwinding the composition root only.
    virtual void releaseAll() = 0;

    /// \brief Cache contents, for the debug overlay and tests.
    virtual AssetStats stats() const = 0;

    /// \brief The catalog this manager was built with.
    virtual const AssetCatalog& catalog() const = 0;
};

/// \brief Creates the asset cache. Returns null when the description is unusable
/// (no catalog or no file system), and logs the reason.
std::unique_ptr<IAssetManager> createAssetManager(const AssetManagerDesc& desc);

}  // namespace fa::assets
