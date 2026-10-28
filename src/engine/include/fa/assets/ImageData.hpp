#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include <fa/core/Math.hpp>
#include <fa/core/Result.hpp>

namespace fa::assets {

/// \brief CPU pixel layout of a decoded image.
///
/// Deliberately separate from `fa::render::TextureFormat` so `fa_assets` never
/// includes `fa_render`: the uploader bridge owns the mapping in one place.
enum class ImageFormat : std::uint8_t {
    /// \brief 8 bits per channel, straight (not premultiplied) alpha.
    Rgba8,
    /// \brief One 8-bit channel: masks, font atlases, height data.
    R8,
    Count,
};

/// \brief Bytes one pixel of \p format occupies, or 0 for an unknown format.
constexpr int bytesPerPixel(ImageFormat format) noexcept {
    switch (format) {
        case ImageFormat::Rgba8:
            return 4;
        case ImageFormat::R8:
            return 1;
        default:
            return 0;
    }
}

/// \brief Stable identifier name for logging and editor UI.
std::string_view toString(ImageFormat format) noexcept;

/// \brief A decoded image owned by the asset cache.
///
/// Pixels are tightly packed and the first row is the top row, which is the layout
/// both the decoders and the GL upload path expect. Sizes are in pixels and always
/// positive for a valid image.
struct ImageData {
    int width = 0;
    int height = 0;
    ImageFormat format = ImageFormat::Rgba8;
    std::vector<std::uint8_t> pixels;

    /// \brief Bytes per row for the tightly packed layout.
    std::size_t rowPitch() const noexcept;

    /// \brief Bytes the size and format require, without considering \ref pixels.
    std::size_t requiredBytes() const noexcept;

    /// \brief Whether the size is positive and \ref pixels is large enough.
    bool isValid() const noexcept;
};

/// \brief Fills a \p width x \p height image with one colour.
///
/// Used for placeholders until real art exists; also the cheapest way for tests and
/// debug overlays to obtain a texture.
ImageData makeSolidImage(int width, int height, core::Color color, ImageFormat format = ImageFormat::Rgba8);

/// \brief Fills a \p width x \p height image with a two-colour checkerboard.
///
/// \param cellSize Side of one square in pixels; values below 1 are treated as 1.
ImageData makeCheckerboardImage(int width, int height, core::Color colorA, core::Color colorB, int cellSize);

/// \brief Parses `#rrggbb` or `#rrggbbaa` (leading `#` optional).
///
/// This is the spelling the catalog uses, so content authors never write floats.
core::Result<core::Color> parseColorText(std::string_view text);

/// \brief Formats \p color as `#rrggbbaa`, the inverse of parseColorText.
std::string toColorText(core::Color color);

}  // namespace fa::assets
