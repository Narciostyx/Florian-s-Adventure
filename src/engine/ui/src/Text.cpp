#include "fa/ui/Text.hpp"

#include <cmath>
#include <cstddef>
#include <string_view>
#include <vector>

namespace fa::ui {

namespace {

struct Line {
    std::size_t start = 0;
    std::size_t end = 0;
    float width = 0.0f;
};

/// \brief Decodes the UTF-8 sequence starting at \p index and advances past it.
///
/// ASCII bytes decode to themselves in one step, so pure-ASCII text behaves exactly as
/// the old byte-wise walk did. A malformed or truncated sequence emits its lead byte and
/// advances one byte, which keeps the walk total and lands on the '?' fallback.
[[nodiscard]] char32_t nextCodepoint(std::string_view text, std::size_t& index) noexcept {
    const auto lead = static_cast<unsigned char>(text[index]);
    std::size_t extra = 0;
    char32_t codepoint = lead;
    if ((lead & 0xE0u) == 0xC0u) {
        extra = 1;
        codepoint = lead & 0x1Fu;
    } else if ((lead & 0xF0u) == 0xE0u) {
        extra = 2;
        codepoint = lead & 0x0Fu;
    } else if ((lead & 0xF8u) == 0xF0u) {
        extra = 3;
        codepoint = lead & 0x07u;
    } else {
        index += 1;
        return lead;
    }

    if (index + extra >= text.size()) {
        index += 1;
        return lead;
    }
    for (std::size_t step = 1; step <= extra; ++step) {
        const auto byte = static_cast<unsigned char>(text[index + step]);
        if ((byte & 0xC0u) != 0x80u) {
            index += 1;
            return lead;
        }
        codepoint = (codepoint << 6) | (byte & 0x3Fu);
    }
    index += extra + 1;
    return codepoint;
}

[[nodiscard]] float effectiveScale(const TextStyle& style) noexcept {
    if (!(style.scale > 0.0f)) {
        return 1.0f;
    }
    const float floored = std::floor(style.scale);
    return floored < 1.0f ? 1.0f : floored;
}

[[nodiscard]] std::vector<Line> layoutLines(const FontAtlas& font, std::string_view text, float maxWidth, float scale) {
    std::vector<Line> lines;
    if (text.empty()) {
        return lines;
    }

    std::size_t start = 0;
    float width = 0.0f;
    std::size_t index = 0;
    while (index < text.size()) {
        const std::size_t glyphStart = index;
        const char32_t codepoint = nextCodepoint(text, index);
        if (codepoint == U'\n') {
            lines.push_back(Line{start, glyphStart, width});
            start = index;
            width = 0.0f;
            continue;
        }
        const float advance = glyphFor(font, codepoint).advance * scale;
        if (maxWidth > 0.0f && width > 0.0f && width + advance > maxWidth) {
            lines.push_back(Line{start, glyphStart, width});
            start = glyphStart;
            width = 0.0f;
        }
        width += advance;
    }
    lines.push_back(Line{start, text.size(), width});
    return lines;
}

}  // namespace

std::string_view fitToCodepoints(std::string_view text, std::size_t maxChars) noexcept {
    std::size_t index = 0;
    for (std::size_t seen = 0; seen < maxChars && index < text.size(); ++seen) {
        static_cast<void>(nextCodepoint(text, index));
    }
    return text.substr(0, index);
}

TextMetrics measureText(const FontAtlas& font, std::string_view text, float maxWidth, const TextStyle& style) {
    TextMetrics metrics;
    if (text.empty()) {
        return metrics;
    }
    const float scale = effectiveScale(style);
    const std::vector<Line> lines = layoutLines(font, text, maxWidth, scale);
    metrics.lines = static_cast<int>(lines.size());
    for (const Line& line : lines) {
        metrics.width = line.width > metrics.width ? line.width : metrics.width;
    }
    metrics.height = static_cast<float>(metrics.lines) * font.lineHeight * scale;
    return metrics;
}

void drawText(
    const FontAtlas& font, std::string_view text, core::Rect rect, const TextStyle& style, render::RenderQueue& queue) {
    if (text.empty() || !font.texture.isValid() || font.lineHeight <= 0.0f) {
        return;
    }

    const float scale = effectiveScale(style);
    const std::vector<Line> lines = layoutLines(font, text, rect.w, scale);
    const core::Color shadowColor{0.0f, 0.0f, 0.0f, 1.0f};

    std::uint32_t nextEntityId = 1;
    for (std::size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        const Line& line = lines[lineIndex];
        float x = rect.x;
        if (style.align == TextAlign::Center) {
            x += (rect.w - line.width) * 0.5f;
        } else if (style.align == TextAlign::Right) {
            x += rect.w - line.width;
        }
        const float y = rect.y + static_cast<float>(lineIndex) * font.lineHeight * scale;

        const auto submit = [&](const Glyph& glyph, float px, float py, core::Color tint) {
            render::DrawItem item;
            item.texture = font.texture;
            // Each glyph is drawn at its own box: a baked atlas mixes 5x7 Latin with 10x9
            // Han, and `bearingY` puts them back on one baseline. The built-in font is
            // uniform with bearingY 0, so this reproduces the old fixed-cell behaviour.
            item.position = core::Vec2{px, py + glyph.bearingY * scale};
            item.size = core::Vec2{glyph.source.w * scale, glyph.source.h * scale};
            item.source = glyph.source;
            item.tint = tint;
            item.layer = render::RenderLayer::UI;
            item.sortY = py;
            item.entityId = nextEntityId++;
            queue.submit(item);
        };

        for (std::size_t index = line.start; index < line.end;) {
            const char32_t codepoint = nextCodepoint(text, index);
            if (codepoint == U' ') {
                x += glyphFor(font, codepoint).advance * scale;
                continue;
            }
            const Glyph& glyph = glyphFor(font, codepoint);
            if (!glyph.defined) {
                x += glyph.advance * scale;
                continue;
            }

            if (style.outline) {
                submit(glyph, x - scale, y, shadowColor);
                submit(glyph, x + scale, y, shadowColor);
                submit(glyph, x, y - scale, shadowColor);
                submit(glyph, x, y + scale, shadowColor);
            } else if (style.shadow) {
                submit(glyph, x + scale, y + scale, shadowColor);
            }
            submit(glyph, x, y, style.color);
            x += glyph.advance * scale;
        }
    }
}

}  // namespace fa::ui
