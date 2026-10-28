#include <fa/assets/ImageData.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include <fa/assets/AssetError.hpp>

namespace fa::assets {

namespace {

std::uint8_t channelToByte(float value) noexcept {
    const float scaled = fa::core::clamp01(value) * 255.0f;
    return static_cast<std::uint8_t>(scaled + 0.5f);
}

int hexDigit(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

}  // namespace

std::string_view toString(ImageFormat format) noexcept {
    switch (format) {
        case ImageFormat::Rgba8:
            return "Rgba8";
        case ImageFormat::R8:
            return "R8";
        default:
            return "Unknown";
    }
}

std::size_t ImageData::rowPitch() const noexcept {
    const int bpp = bytesPerPixel(format);
    if (width <= 0 || bpp <= 0) {
        return 0;
    }
    return static_cast<std::size_t>(width) * static_cast<std::size_t>(bpp);
}

std::size_t ImageData::requiredBytes() const noexcept {
    const std::size_t pitch = rowPitch();
    if (pitch == 0 || height <= 0) {
        return 0;
    }
    return pitch * static_cast<std::size_t>(height);
}

bool ImageData::isValid() const noexcept {
    const std::size_t required = requiredBytes();
    return required != 0 && pixels.size() >= required;
}

ImageData makeSolidImage(int width, int height, core::Color color, ImageFormat format) {
    ImageData image;
    image.width = width;
    image.height = height;
    image.format = format;
    const std::size_t required = image.requiredBytes();
    if (required == 0) {
        return image;
    }
    image.pixels.resize(required);
    if (format == ImageFormat::R8) {
        const std::uint8_t value = channelToByte(color.r);
        for (std::size_t i = 0; i < required; ++i) {
            image.pixels[i] = value;
        }
        return image;
    }
    const std::uint8_t r = channelToByte(color.r);
    const std::uint8_t g = channelToByte(color.g);
    const std::uint8_t b = channelToByte(color.b);
    const std::uint8_t a = channelToByte(color.a);
    for (std::size_t i = 0; i < required; i += 4) {
        image.pixels[i + 0] = r;
        image.pixels[i + 1] = g;
        image.pixels[i + 2] = b;
        image.pixels[i + 3] = a;
    }
    return image;
}

ImageData makeCheckerboardImage(int width, int height, core::Color colorA, core::Color colorB, int cellSize) {
    const ImageData base = makeSolidImage(width, height, colorA);
    ImageData image = base;
    if (!image.isValid()) {
        return image;
    }
    const int cell = cellSize > 0 ? cellSize : 1;
    const std::uint8_t r = channelToByte(colorB.r);
    const std::uint8_t g = channelToByte(colorB.g);
    const std::uint8_t b = channelToByte(colorB.b);
    const std::uint8_t a = channelToByte(colorB.a);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (((x / cell) + (y / cell)) % 2 == 0) {
                continue;
            }
            const std::size_t index = static_cast<std::size_t>(y) * image.rowPitch() + static_cast<std::size_t>(x) * 4;
            image.pixels[index + 0] = r;
            image.pixels[index + 1] = g;
            image.pixels[index + 2] = b;
            image.pixels[index + 3] = a;
        }
    }
    return image;
}

core::Result<core::Color> parseColorText(std::string_view text) {
    if (!text.empty() && text.front() == '#') {
        text.remove_prefix(1);
    }
    if (text.size() != 6 && text.size() != 8) {
        return core::Result<core::Color>::err(asset_error::kInvalidImage, "#rrggbb or #rrggbbaa expected");
    }
    std::uint8_t channels[4] = {0, 0, 0, 255};
    const std::size_t channelCount = text.size() / 2;
    for (std::size_t i = 0; i < channelCount; ++i) {
        const int high = hexDigit(text[i * 2]);
        const int low = hexDigit(text[i * 2 + 1]);
        if (high < 0 || low < 0) {
            return core::Result<core::Color>::err(asset_error::kInvalidImage, "non-hex digit in colour text");
        }
        channels[i] = static_cast<std::uint8_t>(high * 16 + low);
    }
    constexpr float kScale = 1.0f / 255.0f;
    return core::Result<core::Color>::ok(core::Color{static_cast<float>(channels[0]) * kScale,
                                                     static_cast<float>(channels[1]) * kScale,
                                                     static_cast<float>(channels[2]) * kScale,
                                                     static_cast<float>(channels[3]) * kScale});
}

std::string toColorText(core::Color color) {
    char buffer[16] = {};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "#%02x%02x%02x%02x",
                  static_cast<unsigned>(channelToByte(color.r)),
                  static_cast<unsigned>(channelToByte(color.g)),
                  static_cast<unsigned>(channelToByte(color.b)),
                  static_cast<unsigned>(channelToByte(color.a)));
    return std::string(buffer);
}

}  // namespace fa::assets
