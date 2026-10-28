#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "fa/core/Math.hpp"
#include "fa/render/RenderTypes.hpp"

namespace fa::ui {

/// \file
/// Built-in bitmap font.
///
/// The project deliberately ships no font file yet: the atlas is generated from a
/// 3x5 ASCII table at startup, so text works before art exists and tests need no GL.
/// The app uploads `pixels` as one RGBA8 texture (white texels, transparent holes);
/// tinting then colours the glyphs without a custom shader. Real fonts/CJK are a
/// later replacement behind the same `FontAtlas` shape.

/// \brief One glyph in the atlas.
struct Glyph {
    /// \brief Source rectangle in pixels; empty for a missing glyph.
    core::Rect source{};
    /// \brief Horizontal advance in pixels at scale 1.
    float advance = 0.0f;
    /// \brief False when the codepoint has no glyph; `glyphFor()` falls back to '?'.
    bool defined = false;
};

/// \brief CPU atlas plus the runtime texture handle.
struct FontAtlas {
    /// \brief Uploaded texture; invalid until the app calls `createTexture`.
    render::TextureHandle texture;
    int width = 0;
    int height = 0;
    int glyphWidth = 3;
    int glyphHeight = 5;
    /// \brief Baseline-to-baseline distance at scale 1 (glyph height + spacing).
    float lineHeight = 7.0f;
    /// \brief Indexed by `codepoint - 32` for ASCII 32..126.
    std::vector<Glyph> glyphs;
    /// \brief Tightly packed RGBA8 pixels for the upload.
    std::vector<std::uint8_t> pixels;
};

/// \brief Builds the built-in 3x5 ASCII atlas. Lowercase maps to uppercase.
[[nodiscard]] FontAtlas buildBuiltinFont();

/// \brief Glyph for a codepoint, falling back to '?' for unknown characters.
[[nodiscard]] const Glyph& glyphFor(const FontAtlas& font, char32_t codepoint) noexcept;

/// \brief First codepoint in the atlas.
inline constexpr char32_t kFirstCodepoint = 32;

/// \brief Last codepoint in the atlas.
inline constexpr char32_t kLastCodepoint = 126;

/// \brief Glyphs in the atlas.
inline constexpr int kGlyphCount = static_cast<int>(kLastCodepoint - kFirstCodepoint) + 1;

/// \brief Horizontal cell size (glyph width plus one pixel of spacing).
inline constexpr int kGlyphAdvance = 4;

}  // namespace fa::ui
