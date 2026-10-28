#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include <fa/assets/IImageDecoder.hpp>

namespace fa::app {

/// \brief stb_image-backed decoder for PNG/JPG/TGA/BMP assets.
///
/// The app registers this with `AssetManagerDesc::decoders`; M05 still owns the
/// fallback chain, so a missing file or a failed decode keeps the procedural
/// placeholder instead of failing startup.
class StbImageDecoder final : public assets::IImageDecoder {
public:
    [[nodiscard]] bool supports(std::string_view path) const override;
    [[nodiscard]] core::Result<assets::ImageData> decode(std::string_view path,
                                                         std::span<const std::uint8_t> bytes) override;
};

}  // namespace fa::app
