#include "fa/ui/Font.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace fa::ui {

namespace {

/// \brief Five rows of three bits; bit 2 is the leftmost pixel.
struct GlyphRows {
    std::uint8_t row[5]{};
};

char normalizeCodepoint(char32_t codepoint) noexcept {
    if (codepoint >= U'a' && codepoint <= U'z') {
        return static_cast<char>(codepoint - U'a' + U'A');
    }
    if (codepoint < kFirstCodepoint || codepoint > kLastCodepoint) {
        return '?';
    }
    return static_cast<char>(codepoint);
}

bool rowsFor(char c, GlyphRows& out) noexcept {
    switch (c) {
        case ' ':
            out = GlyphRows{{0, 0, 0, 0, 0}};
            return true;
        case '0':
            out = GlyphRows{{0b111, 0b101, 0b101, 0b101, 0b111}};
            return true;
        case '1':
            out = GlyphRows{{0b010, 0b110, 0b010, 0b010, 0b111}};
            return true;
        case '2':
            out = GlyphRows{{0b111, 0b001, 0b111, 0b100, 0b111}};
            return true;
        case '3':
            out = GlyphRows{{0b111, 0b001, 0b111, 0b001, 0b111}};
            return true;
        case '4':
            out = GlyphRows{{0b101, 0b101, 0b111, 0b001, 0b001}};
            return true;
        case '5':
            out = GlyphRows{{0b111, 0b100, 0b111, 0b001, 0b111}};
            return true;
        case '6':
            out = GlyphRows{{0b111, 0b100, 0b111, 0b101, 0b111}};
            return true;
        case '7':
            out = GlyphRows{{0b111, 0b001, 0b010, 0b010, 0b010}};
            return true;
        case '8':
            out = GlyphRows{{0b111, 0b101, 0b111, 0b101, 0b111}};
            return true;
        case '9':
            out = GlyphRows{{0b111, 0b101, 0b111, 0b001, 0b111}};
            return true;
        case 'A':
            out = GlyphRows{{0b010, 0b101, 0b111, 0b101, 0b101}};
            return true;
        case 'B':
            out = GlyphRows{{0b110, 0b101, 0b110, 0b101, 0b110}};
            return true;
        case 'C':
            out = GlyphRows{{0b011, 0b100, 0b100, 0b100, 0b011}};
            return true;
        case 'D':
            out = GlyphRows{{0b110, 0b101, 0b101, 0b101, 0b110}};
            return true;
        case 'E':
            out = GlyphRows{{0b111, 0b100, 0b110, 0b100, 0b111}};
            return true;
        case 'F':
            out = GlyphRows{{0b111, 0b100, 0b110, 0b100, 0b100}};
            return true;
        case 'G':
            out = GlyphRows{{0b011, 0b100, 0b101, 0b101, 0b011}};
            return true;
        case 'H':
            out = GlyphRows{{0b101, 0b101, 0b111, 0b101, 0b101}};
            return true;
        case 'I':
            out = GlyphRows{{0b111, 0b010, 0b010, 0b010, 0b111}};
            return true;
        case 'J':
            out = GlyphRows{{0b001, 0b001, 0b001, 0b101, 0b010}};
            return true;
        case 'K':
            out = GlyphRows{{0b101, 0b101, 0b110, 0b101, 0b101}};
            return true;
        case 'L':
            out = GlyphRows{{0b100, 0b100, 0b100, 0b100, 0b111}};
            return true;
        case 'M':
            out = GlyphRows{{0b101, 0b111, 0b111, 0b101, 0b101}};
            return true;
        case 'N':
            out = GlyphRows{{0b101, 0b111, 0b111, 0b111, 0b101}};
            return true;
        case 'O':
            out = GlyphRows{{0b010, 0b101, 0b101, 0b101, 0b010}};
            return true;
        case 'P':
            out = GlyphRows{{0b110, 0b101, 0b110, 0b100, 0b100}};
            return true;
        case 'Q':
            out = GlyphRows{{0b010, 0b101, 0b101, 0b111, 0b011}};
            return true;
        case 'R':
            out = GlyphRows{{0b110, 0b101, 0b110, 0b101, 0b101}};
            return true;
        case 'S':
            out = GlyphRows{{0b011, 0b100, 0b010, 0b001, 0b110}};
            return true;
        case 'T':
            out = GlyphRows{{0b111, 0b010, 0b010, 0b010, 0b010}};
            return true;
        case 'U':
            out = GlyphRows{{0b101, 0b101, 0b101, 0b101, 0b111}};
            return true;
        case 'V':
            out = GlyphRows{{0b101, 0b101, 0b101, 0b101, 0b010}};
            return true;
        case 'W':
            out = GlyphRows{{0b101, 0b101, 0b111, 0b111, 0b101}};
            return true;
        case 'X':
            out = GlyphRows{{0b101, 0b101, 0b010, 0b101, 0b101}};
            return true;
        case 'Y':
            out = GlyphRows{{0b101, 0b101, 0b010, 0b010, 0b010}};
            return true;
        case 'Z':
            out = GlyphRows{{0b111, 0b001, 0b010, 0b100, 0b111}};
            return true;
        case '!':
            out = GlyphRows{{0b010, 0b010, 0b010, 0b000, 0b010}};
            return true;
        case '"':
            out = GlyphRows{{0b101, 0b101, 0b000, 0b000, 0b000}};
            return true;
        case '#':
            out = GlyphRows{{0b101, 0b111, 0b101, 0b111, 0b101}};
            return true;
        case '$':
            out = GlyphRows{{0b011, 0b110, 0b010, 0b011, 0b110}};
            return true;
        case '%':
            out = GlyphRows{{0b101, 0b001, 0b010, 0b100, 0b101}};
            return true;
        case '&':
            out = GlyphRows{{0b010, 0b101, 0b010, 0b101, 0b011}};
            return true;
        case '\'':
            out = GlyphRows{{0b010, 0b010, 0b000, 0b000, 0b000}};
            return true;
        case '(':
            out = GlyphRows{{0b001, 0b010, 0b010, 0b010, 0b001}};
            return true;
        case ')':
            out = GlyphRows{{0b100, 0b010, 0b010, 0b010, 0b100}};
            return true;
        case '*':
            out = GlyphRows{{0b101, 0b010, 0b111, 0b010, 0b101}};
            return true;
        case '+':
            out = GlyphRows{{0b000, 0b010, 0b111, 0b010, 0b000}};
            return true;
        case ',':
            out = GlyphRows{{0b000, 0b000, 0b000, 0b010, 0b010}};
            return true;
        case '-':
            out = GlyphRows{{0b000, 0b000, 0b111, 0b000, 0b000}};
            return true;
        case '.':
            out = GlyphRows{{0b000, 0b000, 0b000, 0b000, 0b010}};
            return true;
        case '/':
            out = GlyphRows{{0b001, 0b001, 0b010, 0b100, 0b100}};
            return true;
        case ':':
            out = GlyphRows{{0b000, 0b010, 0b000, 0b010, 0b000}};
            return true;
        case ';':
            out = GlyphRows{{0b000, 0b010, 0b000, 0b010, 0b010}};
            return true;
        case '<':
            out = GlyphRows{{0b001, 0b010, 0b100, 0b010, 0b001}};
            return true;
        case '=':
            out = GlyphRows{{0b000, 0b111, 0b000, 0b111, 0b000}};
            return true;
        case '>':
            out = GlyphRows{{0b100, 0b010, 0b001, 0b010, 0b100}};
            return true;
        case '?':
            out = GlyphRows{{0b111, 0b001, 0b011, 0b000, 0b010}};
            return true;
        case '@':
            out = GlyphRows{{0b111, 0b101, 0b111, 0b100, 0b011}};
            return true;
        case '[':
            out = GlyphRows{{0b011, 0b010, 0b010, 0b010, 0b011}};
            return true;
        case '\\':
            out = GlyphRows{{0b100, 0b100, 0b010, 0b001, 0b001}};
            return true;
        case ']':
            out = GlyphRows{{0b110, 0b010, 0b010, 0b010, 0b110}};
            return true;
        case '^':
            out = GlyphRows{{0b010, 0b101, 0b000, 0b000, 0b000}};
            return true;
        case '_':
            out = GlyphRows{{0b000, 0b000, 0b000, 0b000, 0b111}};
            return true;
        case '`':
            out = GlyphRows{{0b100, 0b010, 0b000, 0b000, 0b000}};
            return true;
        case '{':
            out = GlyphRows{{0b001, 0b010, 0b110, 0b010, 0b001}};
            return true;
        case '|':
            out = GlyphRows{{0b010, 0b010, 0b010, 0b010, 0b010}};
            return true;
        case '}':
            out = GlyphRows{{0b100, 0b010, 0b011, 0b010, 0b100}};
            return true;
        case '~':
            out = GlyphRows{{0b000, 0b101, 0b111, 0b101, 0b000}};
            return true;
        default:
            return false;
    }
}

}  // namespace

FontAtlas buildBuiltinFont() {
    FontAtlas font;
    font.width = kGlyphCount * kGlyphAdvance;
    font.height = 5;
    font.glyphWidth = 3;
    font.glyphHeight = 5;
    font.lineHeight = 7.0f;
    font.glyphs.resize(static_cast<std::size_t>(kGlyphCount));
    font.pixels.assign(static_cast<std::size_t>(font.width) * static_cast<std::size_t>(font.height) * 4u, 0);

    GlyphRows fallback{};
    static_cast<void>(rowsFor('?', fallback));

    for (int index = 0; index < kGlyphCount; ++index) {
        const char codepoint = static_cast<char>(kFirstCodepoint + index);
        GlyphRows rows{};
        const bool defined = rowsFor(codepoint, rows);
        if (!defined) {
            rows = fallback;
        }

        Glyph& glyph = font.glyphs[static_cast<std::size_t>(index)];
        glyph.source = core::makeRect(static_cast<float>(index * kGlyphAdvance), 0.0f, 3.0f, 5.0f);
        glyph.advance = static_cast<float>(kGlyphAdvance);
        glyph.defined = defined;

        for (int row = 0; row < 5; ++row) {
            for (int column = 0; column < 3; ++column) {
                if ((rows.row[row] & (1u << (2 - column))) == 0) {
                    continue;
                }
                const std::size_t pixel = (static_cast<std::size_t>(row) * static_cast<std::size_t>(font.width) +
                                           static_cast<std::size_t>(index * kGlyphAdvance + column)) *
                                          4u;
                font.pixels[pixel + 0] = 255;
                font.pixels[pixel + 1] = 255;
                font.pixels[pixel + 2] = 255;
                font.pixels[pixel + 3] = 255;
            }
        }
    }
    return font;
}

const Glyph& glyphFor(const FontAtlas& font, char32_t codepoint) noexcept {
    if (font.glyphs.size() < static_cast<std::size_t>(kGlyphCount)) {
        static const Glyph kEmpty{};
        return kEmpty;
    }
    const char normalized = normalizeCodepoint(codepoint);
    const std::size_t index = static_cast<std::size_t>(normalized - static_cast<char>(kFirstCodepoint));
    const Glyph& glyph = font.glyphs[index];
    if (glyph.defined) {
        return glyph;
    }
    return font.glyphs[static_cast<std::size_t>('?' - static_cast<char>(kFirstCodepoint))];
}

}  // namespace fa::ui
