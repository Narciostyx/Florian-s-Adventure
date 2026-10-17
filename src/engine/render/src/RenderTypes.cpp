#include <fa/render/RenderTypes.hpp>

namespace fa::render {

std::string_view toString(TextureFormat format) noexcept {
    switch (format) {
        case TextureFormat::Rgba8:
            return "Rgba8";
        case TextureFormat::R8:
            return "R8";
        default:
            return "Unknown";
    }
}

std::string_view toString(RenderLayer layer) noexcept {
    switch (layer) {
        case RenderLayer::Background:
            return "Background";
        case RenderLayer::Ground:
            return "Ground";
        case RenderLayer::GroundDetail:
            return "GroundDetail";
        case RenderLayer::Objects:
            return "Objects";
        case RenderLayer::Actors:
            return "Actors";
        case RenderLayer::Foreground:
            return "Foreground";
        case RenderLayer::Weather:
            return "Weather";
        case RenderLayer::UI:
            return "UI";
        default:
            return "Unknown";
    }
}

int TextureDesc::effectiveRowPitch() const noexcept {
    if (rowPitchBytes > 0) {
        return rowPitchBytes;
    }
    return width * bytesPerPixel(format);
}

}  // namespace fa::render
