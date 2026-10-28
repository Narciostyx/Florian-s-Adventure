#pragma once

#include <cstdint>
#include <string_view>

#include <fa/core/Ids.hpp>
#include <fa/core/Math.hpp>

namespace fa::render {

/// \brief Tag type for TextureHandle; never instantiated.
struct TextureTag;

/// \brief Opaque reference to a GPU texture.
///
/// Zero is invalid. The renderer owns the texture; the handle carries no pointer,
/// so a stale handle can only ever be reported as an error and never dereferenced.
using TextureHandle = fa::core::StrongId<TextureTag>;

/// \brief Pixel layout of an uploaded texture.
enum class TextureFormat : std::uint8_t {
    /// 8 bits per channel, straight (not premultiplied) alpha; the default.
    Rgba8,
    /// Single 8-bit channel: masks, font atlases, height data.
    R8,
    Count,
};

/// \brief Bytes one pixel of \p format occupies, or 0 for an unknown format.
constexpr int bytesPerPixel(TextureFormat format) noexcept {
    switch (format) {
        case TextureFormat::Rgba8:
            return 4;
        case TextureFormat::R8:
            return 1;
        default:
            return 0;
    }
}

/// \brief Stable identifier name for logging and editor UI.
std::string_view toString(TextureFormat format) noexcept;

/// \brief Upload description for one texture.
struct TextureDesc {
    int width = 0;
    int height = 0;
    TextureFormat format = TextureFormat::Rgba8;
    /// Straight rgba8 or r8 pixels, first row = top row, tightly packed unless
    /// \ref rowPitchBytes says otherwise. Null leaves the contents undefined,
    /// which is only useful for render targets added by a later pass.
    const std::uint8_t* pixels = nullptr;
    /// Bytes per row in \ref pixels; 0 means `width * bytesPerPixel(format)`.
    int rowPitchBytes = 0;
    /// For logs only; never used to look a texture up.
    std::string_view debugName{};

    /// \brief Bytes per row that \ref pixels is actually read with.
    int effectiveRowPitch() const noexcept;
};

/// \brief Painter's-order layer, coarsest part of the draw sort key.
///
/// The numeric order is the draw order, so `Background` is always behind `UI`
/// regardless of world position.
enum class RenderLayer : std::uint8_t {
    Background,
    Ground,
    GroundDetail,
    Objects,
    Actors,
    Foreground,
    Weather,
    UI,
    Count,
};

/// \brief Stable identifier name for logging and editor UI.
std::string_view toString(RenderLayer layer) noexcept;

/// \brief One sprite (or coloured quad) submitted for this frame.
///
/// Submitted in any order; the render queue sorts before drawing.
struct DrawItem {
    TextureHandle texture{};
    /// World-space top-left corner before \ref rotation.
    core::Vec2 position{};
    /// World-space size in virtual pixels.
    core::Vec2 size{1.0f, 1.0f};
    /// Source rectangle in texture pixels. An empty rect means "the whole
    /// texture", which is what callers that never use atlases want.
    core::Rect source{};
    /// Multiplied with the sampled texel; alpha is not premultiplied.
    core::Color tint{1.0f, 1.0f, 1.0f, 1.0f};
    /// Clockwise around the item centre, in radians. Reserved for effects:
    /// pixel art only stays crisp at multiples of 90 degrees.
    float rotation = 0.0f;
    bool flipX = false;
    bool flipY = false;
    RenderLayer layer = RenderLayer::Objects;
    /// Depth inside the layer, normally the feet y. Quantised to whole pixels
    /// before sorting so two actors on the same row never swap per frame.
    float sortY = 0.0f;
    /// Optional owner id (`0` = none) that breaks ties between equal sort keys,
    /// so the draw order does not depend on submission order. M11 fills this.
    std::uint32_t entityId = 0;
};

/// \brief The quantised depth value the draw sort actually compares.
constexpr std::int32_t depthKey(float sortY) noexcept {
    return static_cast<std::int32_t>(sortY < 0.0f ? sortY - 0.5f : sortY + 0.5f);
}

/// \brief Draw order: layer, then quantised depth, then entity id.
///
/// Callers that need a total order still have to use a stable sort, because two
/// items may agree on all three fields.
constexpr bool drawsBefore(const DrawItem& lhs, const DrawItem& rhs) noexcept {
    if (lhs.layer != rhs.layer) {
        return static_cast<std::uint8_t>(lhs.layer) < static_cast<std::uint8_t>(rhs.layer);
    }
    const std::int32_t lhsDepth = depthKey(lhs.sortY);
    const std::int32_t rhsDepth = depthKey(rhs.sortY);
    if (lhsDepth != rhsDepth) {
        return lhsDepth < rhsDepth;
    }
    return lhs.entityId < rhs.entityId;
}

/// \brief What the last completed frame actually cost.
struct RenderStats {
    std::uint32_t drawCalls = 0;
    std::uint32_t quads = 0;
    std::uint32_t textureSwitches = 0;
    std::uint32_t vertices = 0;
    /// Items the queue refused because the frame budget was exhausted.
    std::uint32_t droppedItems = 0;

    void reset() noexcept { *this = RenderStats{}; }
};

}  // namespace fa::render
