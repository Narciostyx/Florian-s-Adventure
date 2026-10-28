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
    for (std::size_t index = 0; index < text.size(); ++index) {
        const auto codepoint = static_cast<char32_t>(static_cast<unsigned char>(text[index]));
        if (codepoint == U'\n') {
            lines.push_back(Line{start, index, width});
            start = index + 1;
            width = 0.0f;
            continue;
        }
        const float advance = glyphFor(font, codepoint).advance * scale;
        if (maxWidth > 0.0f && width > 0.0f && width + advance > maxWidth) {
            lines.push_back(Line{start, index, width});
            start = index;
            width = 0.0f;
        }
        width += advance;
    }
    lines.push_back(Line{start, text.size(), width});
    return lines;
}

}  // namespace

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
    const float glyphWidth = static_cast<float>(font.glyphWidth) * scale;
    const float glyphHeight = static_cast<float>(font.glyphHeight) * scale;
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
            item.position = core::Vec2{px, py};
            item.size = core::Vec2{glyphWidth, glyphHeight};
            item.source = glyph.source;
            item.tint = tint;
            item.layer = render::RenderLayer::UI;
            item.sortY = py;
            item.entityId = nextEntityId++;
            queue.submit(item);
        };

        for (std::size_t index = line.start; index < line.end; ++index) {
            const auto codepoint = static_cast<char32_t>(static_cast<unsigned char>(text[index]));
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
