#pragma once

#include <cstdint>
#include <string_view>

#include <fa/assets/ImageData.hpp>

namespace fa::assets {

/// \brief Bridge that turns CPU pixels into a GPU object.
///
/// `fa_assets` must not depend on `fa_render` (see docs/architecture/dependency-rules.md),
/// so the texture call is inverted here: the composition root registers an adapter
/// backed by `fa::render::IRenderer`, and the cache calls it on the frame thread.
/// The token is opaque to this module - it is exactly
/// `fa::render::TextureHandle::value()`, and zero means "no GPU object".
class ITextureUploader {
public:
    ITextureUploader() = default;
    virtual ~ITextureUploader() = default;
    ITextureUploader(const ITextureUploader&) = delete;
    ITextureUploader& operator=(const ITextureUploader&) = delete;

    /// \brief Uploads \p image and returns the token, or 0 on failure.
    ///
    /// Must be called on the thread that owns the GL context.
    virtual std::uint32_t upload(const ImageData& image, std::string_view debugName) = 0;

    /// \brief Releases a token previously returned by upload(). Zero is ignored.
    virtual void release(std::uint32_t token) = 0;
};

}  // namespace fa::assets
