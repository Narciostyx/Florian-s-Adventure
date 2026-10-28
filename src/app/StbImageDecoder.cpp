#include "StbImageDecoder.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>

#include <fa/assets/AssetError.hpp>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4100 4244 4245 4267 4456 4457 4701 4702 4703 4996)
#endif
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace fa::app {
namespace {

std::string extensionOf(std::string_view path) {
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string_view::npos) {
        return {};
    }
    std::string extension(path.substr(dot));
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension;
}

}  // namespace

bool StbImageDecoder::supports(std::string_view path) const {
    const std::string extension = extensionOf(path);
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" ||
           extension == ".bmp";
}

core::Result<assets::ImageData> StbImageDecoder::decode(std::string_view path, std::span<const std::uint8_t> bytes) {
    if (bytes.empty()) {
        return core::Result<assets::ImageData>::err(assets::asset_error::kDecodeFailed, "image file is empty");
    }
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels =
        stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4);
    if (pixels == nullptr) {
        const char* reason = stbi_failure_reason();
        return core::Result<assets::ImageData>::err(assets::asset_error::kDecodeFailed,
                                                    "stb_image failed on " + std::string(path) + ": " +
                                                        (reason != nullptr ? reason : "unknown error"));
    }

    assets::ImageData image;
    image.width = width;
    image.height = height;
    image.format = assets::ImageFormat::Rgba8;
    const std::size_t size = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    image.pixels.assign(pixels, pixels + size);
    stbi_image_free(pixels);

    if (!image.isValid()) {
        return core::Result<assets::ImageData>::err(assets::asset_error::kInvalidImage, "decoded image is unusable");
    }
    return core::Result<assets::ImageData>::ok(std::move(image));
}

}  // namespace fa::app
