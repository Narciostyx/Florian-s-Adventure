#pragma once

#include <cstdint>
#include <string_view>

#include "fa/core/Math.hpp"
#include "fa/render/RenderQueue.hpp"
#include "fa/ui/Font.hpp"

namespace fa::ui {

/// \file
/// Text measurement and drawing into a `RenderQueue`.
///
/// Layout is deliberately data-only: no renderer, no GL, no allocations in the
/// renderer path beyond the line list. v1 wraps by character; word wrapping and
/// rich text are follow-ups. Scales should be whole numbers to keep the 3x5 font
/// crisp; non-integer scales are floored to at least 1.

/// \brief Horizontal alignment inside the supplied rectangle.
enum class TextAlign : std::uint8_t {
    Left,
    Center,
    Right,
};

/// \brief Colour/scale/alignment and the two cheap effects.
struct TextStyle {
    core::Color color{1.0f, 1.0f, 1.0f, 1.0f};
    float scale = 1.0f;
    TextAlign align = TextAlign::Left;
    /// \brief One-pixel drop shadow behind every glyph.
    bool shadow = false;
    /// \brief Four-pixel black outline behind every glyph; takes precedence over shadow.
    bool outline = false;
};

/// \brief Result of a measurement pass.
struct TextMetrics {
    /// \brief Width of the widest line, in virtual pixels.
    float width = 0.0f;
    /// \brief Total height, including line spacing.
    float height = 0.0f;
    int lines = 0;
};

/// \brief Truncates a UTF-8 string on a codepoint boundary, keeping at most `maxChars`
/// characters.
///
/// `std::string_view::substr(n)` counts bytes, so with 3-byte Han characters it can cut a
/// codepoint in half and M09 then draws a `?`. Display strings that have to be shortened to
/// fit a row go through here instead. Invalid bytes are treated as one character each, so
/// the result is always a prefix of the input and never reads past it.
[[nodiscard]] std::string_view fitToCodepoints(std::string_view text, std::size_t maxChars) noexcept;

/// \brief Measures \p text, wrapping at \p maxWidth when it is positive.
[[nodiscard]] TextMetrics
measureText(const FontAtlas& font, std::string_view text, float maxWidth, const TextStyle& style = {});

/// \brief Appends one `DrawItem` per visible glyph to \p queue.
///
/// All items use `RenderLayer::UI`; `sortY` is the glyph's y (kept for the world-space
/// callers and for debug tooling), but the UI layer draws in submission order, so a label
/// is never repainted by the widget behind it. `font.texture` must be valid.
void drawText(
    const FontAtlas& font, std::string_view text, core::Rect rect, const TextStyle& style, render::RenderQueue& queue);

}  // namespace fa::ui
