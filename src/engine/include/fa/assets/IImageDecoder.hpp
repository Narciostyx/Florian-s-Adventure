#pragma once

#include <span>
#include <string_view>

#include <fa/assets/ImageData.hpp>
#include <fa/core/Result.hpp>

namespace fa::assets {

/// \brief Turns source bytes into pixels.
///
/// One decoder per file family. v1 ships no decoder, because the project has no art
/// yet: every texture resolves to its catalog placeholder. Adding PNG support later
/// is a new implementation registered in `AssetManagerDesc::decoders` - no change to
/// the cache, the handles or any caller.
class IImageDecoder {
public:
    IImageDecoder() = default;
    virtual ~IImageDecoder() = default;
    IImageDecoder(const IImageDecoder&) = delete;
    IImageDecoder& operator=(const IImageDecoder&) = delete;

    /// \brief Whether this decoder handles \p path, judged by its extension.
    virtual bool supports(std::string_view path) const = 0;

    /// \brief Decodes \p bytes into a valid image, or explains why it could not.
    virtual core::Result<ImageData> decode(std::string_view path, std::span<const std::uint8_t> bytes) = 0;
};

}  // namespace fa::assets
