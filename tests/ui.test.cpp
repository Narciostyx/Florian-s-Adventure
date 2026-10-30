/// \file
/// \brief Tests for M09: the built-in bitmap font, text measurement/layout, glyph
/// submission and the immediate-mode widgets. Everything is headless: the UI only
/// builds `DrawItem`s, so these tests need no GL context.
/// See docs/build/m09-ui-and-text.md for the problems found while writing this.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include <fa/core/Math.hpp>
#include <fa/render/RenderQueue.hpp>
#include <fa/render/RenderTypes.hpp>
#include <fa/ui/Font.hpp>
#include <fa/ui/Text.hpp>
#include <fa/ui/Ui.hpp>

namespace {

using Catch::Approx;
using fa::core::Color;
using fa::core::Rect;
using fa::core::Vec2;
using fa::render::DrawItem;
using fa::render::RenderLayer;
using fa::render::RenderQueue;
using fa::render::TextureHandle;
using fa::ui::buildBuiltinFont;
using fa::ui::FontAtlas;
using fa::ui::Glyph;
using fa::ui::glyphFor;
using fa::ui::kFirstCodepoint;
using fa::ui::kGlyphCount;
using fa::ui::measureText;
using fa::ui::TextAlign;
using fa::ui::TextMetrics;
using fa::ui::TextStyle;
using fa::ui::UiContext;
using fa::ui::UiId;
using fa::ui::UiInputState;
using fa::ui::UiResult;

/// \brief A font whose texture handle is non-zero so drawText() actually submits.
FontAtlas testFont() {
    FontAtlas font = buildBuiltinFont();
    font.texture = TextureHandle{1};
    return font;
}

constexpr std::uint32_t kWhiteTexture = 2;

UiInputState mouseAt(Vec2 position, bool down = false, bool pressed = false, bool released = false) {
    UiInputState input;
    input.mousePosition = position;
    input.mouseDown = down;
    input.mousePressed = pressed;
    input.mouseReleased = released;
    return input;
}

std::span<const DrawItem> itemsOf(const RenderQueue& queue) {
    return queue.items();
}

}  // namespace

TEST_CASE("builtin font builds a complete ASCII atlas", "[ui]") {
    const FontAtlas font = buildBuiltinFont();

    CHECK(font.glyphWidth == 3);
    CHECK(font.glyphHeight == 5);
    CHECK(font.lineHeight == Approx(7.0f));
    CHECK(font.width == kGlyphCount * fa::ui::kGlyphAdvance);
    CHECK(font.height == 5);
    CHECK(font.glyphs.size() == static_cast<std::size_t>(kGlyphCount));
    CHECK(font.pixels.size() == static_cast<std::size_t>(font.width) * static_cast<std::size_t>(font.height) * 4u);
    CHECK(font.texture.isValid() == false);

    const Glyph& upper = glyphFor(font, U'A');
    CHECK(upper.defined);
    CHECK(upper.advance == Approx(static_cast<float>(fa::ui::kGlyphAdvance)));
    CHECK(upper.source.x == Approx(static_cast<float>((U'A' - kFirstCodepoint) * fa::ui::kGlyphAdvance)));

    const Glyph& space = glyphFor(font, U' ');
    CHECK(space.defined);
    CHECK(space.source.w == Approx(3.0f));

    // 'A' is the first letter with pixels: row 1 is 010, row 2 is 101, row 3 is 111.
    const int column = (U'A' - kFirstCodepoint) * fa::ui::kGlyphAdvance;
    const auto pixelSet = [&font, column](int x, int y) {
        const std::size_t index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(font.width) +
                                   static_cast<std::size_t>(column + x)) *
                                  4u;
        return font.pixels[index + 3] != 0;
    };
    CHECK_FALSE(pixelSet(0, 0));
    CHECK(pixelSet(1, 0));
    CHECK(pixelSet(0, 1));
}

TEST_CASE("glyph lookup normalises lowercase and falls back to '?'", "[ui]") {
    const FontAtlas font = buildBuiltinFont();

    const Glyph& lower = glyphFor(font, U'a');
    const Glyph& upper = glyphFor(font, U'A');
    CHECK(lower.source.x == Approx(upper.source.x));
    CHECK(lower.source.y == Approx(upper.source.y));
    CHECK(lower.defined);

    const Glyph& question = glyphFor(font, U'?');
    CHECK(question.defined);
    const Glyph& unknown = glyphFor(font, U'\u4e2d');
    // The fallback glyph itself is defined; only an unusable atlas returns an
    // undefined glyph.
    CHECK(unknown.defined);
    CHECK(unknown.source.x == Approx(question.source.x));

    const FontAtlas empty;
    const Glyph& missing = glyphFor(empty, U'A');
    CHECK(missing.defined == false);
    CHECK(missing.advance == Approx(0.0f));
}

TEST_CASE("measureText reports width, height and line count", "[ui]") {
    const FontAtlas font = buildBuiltinFont();

    const TextMetrics empty = measureText(font, "", 0.0f, {});
    CHECK(empty.width == Approx(0.0f));
    CHECK(empty.height == Approx(0.0f));
    CHECK(empty.lines == 0);

    const TextMetrics two = measureText(font, "AB", 0.0f, {});
    CHECK(two.width == Approx(8.0f));
    CHECK(two.height == Approx(7.0f));
    CHECK(two.lines == 1);

    const TextMetrics newline = measureText(font, "A\nB", 0.0f, {});
    CHECK(newline.lines == 2);
    CHECK(newline.width == Approx(4.0f));
    CHECK(newline.height == Approx(14.0f));

    TextStyle scaled;
    scaled.scale = 2.9f;
    const TextMetrics scaledMetrics = measureText(font, "AB", 0.0f, scaled);
    CHECK(scaledMetrics.width == Approx(16.0f));
    CHECK(scaledMetrics.height == Approx(14.0f));

    TextStyle tiny;
    tiny.scale = 0.0f;
    const TextMetrics tinyMetrics = measureText(font, "AB", 0.0f, tiny);
    CHECK(tinyMetrics.width == Approx(8.0f));
}

TEST_CASE("text wraps at the supplied width", "[ui]") {
    const FontAtlas font = buildBuiltinFont();

    const TextMetrics wrapped = measureText(font, "ABCD", 8.0f, {});
    CHECK(wrapped.lines == 2);
    CHECK(wrapped.width == Approx(8.0f));
    CHECK(wrapped.height == Approx(14.0f));

    // A single word that is wider than the box still gets split per character.
    const TextMetrics word = measureText(font, "ABCDE", 4.0f, {});
    CHECK(word.lines == 5);
    CHECK(word.width == Approx(4.0f));
}

TEST_CASE("drawText submits one item per glyph on the UI layer", "[ui]") {
    const FontAtlas font = testFont();
    RenderQueue queue;

    drawText(font, "A B", Rect{10.0f, 20.0f, 100.0f, 20.0f}, {}, queue);

    const std::span<const DrawItem> items = itemsOf(queue);
    REQUIRE(items.size() == 2);
    CHECK(items[0].position.x == Approx(10.0f));
    CHECK(items[0].position.y == Approx(20.0f));
    CHECK(items[1].position.x == Approx(18.0f));
    CHECK(items[0].layer == RenderLayer::UI);
    CHECK(items[0].texture.isValid());
    CHECK(items[0].size.x == Approx(3.0f));
    CHECK(items[0].size.y == Approx(5.0f));
    CHECK(items[0].source.w == Approx(3.0f));
    CHECK(items[0].source.h == Approx(5.0f));
    CHECK(items[0].entityId != items[1].entityId);
}

TEST_CASE("drawText honours alignment and integer scaling", "[ui]") {
    const FontAtlas font = testFont();
    TextStyle style;
    style.align = TextAlign::Center;
    style.scale = 2.0f;

    RenderQueue center;
    drawText(font, "A", Rect{10.0f, 0.0f, 20.0f, 20.0f}, style, center);
    REQUIRE(center.size() == 1);
    // Alignment uses the advance (8 px at scale 2), not the 6 px visual width.
    CHECK(center.items()[0].position.x == Approx(16.0f));
    CHECK(center.items()[0].size.x == Approx(6.0f));

    style.align = TextAlign::Right;
    RenderQueue right;
    drawText(font, "A", Rect{10.0f, 0.0f, 20.0f, 20.0f}, style, right);
    REQUIRE(right.size() == 1);
    CHECK(right.items()[0].position.x == Approx(22.0f));
}

TEST_CASE("drawText supports shadow and outline effects", "[ui]") {
    const FontAtlas font = testFont();

    TextStyle shadow;
    shadow.shadow = true;
    RenderQueue shadowQueue;
    drawText(font, "A", Rect{0.0f, 0.0f, 100.0f, 20.0f}, shadow, shadowQueue);
    CHECK(shadowQueue.size() == 2);
    CHECK(shadowQueue.items()[0].tint.r == Approx(0.0f));
    CHECK(shadowQueue.items()[1].tint == shadow.color);

    TextStyle outline;
    outline.outline = true;
    RenderQueue outlineQueue;
    drawText(font, "A", Rect{0.0f, 0.0f, 100.0f, 20.0f}, outline, outlineQueue);
    CHECK(outlineQueue.size() == 5);
    CHECK(outlineQueue.items()[4].tint == outline.color);
}

TEST_CASE("drawText needs a texture and falls back for unknown bytes", "[ui]") {
    FontAtlas font = buildBuiltinFont();
    RenderQueue queue;
    drawText(font, "A", Rect{0.0f, 0.0f, 10.0f, 10.0f}, {}, queue);
    CHECK(queue.empty());

    font.texture = TextureHandle{1};
    // v1 walks UTF-8 bytes as bytes; one byte outside the atlas maps to '?'.
    drawText(font, "\x7f", Rect{0.0f, 0.0f, 10.0f, 10.0f}, {}, queue);
    CHECK(queue.size() == 1);
}

TEST_CASE("UiId derives stable, non-zero hashes", "[ui]") {
    CHECK(UiId::fromName("play") == UiId::fromName("play"));
    CHECK(UiId::fromName("play") != UiId::fromName("pause"));
    CHECK(UiId::fromName("").value != 0u);
}

TEST_CASE("panels, borders and bars submit UI quads", "[ui]") {
    const FontAtlas font = testFont();
    UiContext ui(font, TextureHandle{kWhiteTexture});
    ui.beginFrame(mouseAt(Vec2{0.0f, 0.0f}));

    ui.panel(Rect{0.0f, 0.0f, 20.0f, 10.0f}, Color{1.0f, 0.0f, 0.0f, 1.0f});
    REQUIRE(ui.itemCount() == 1);
    CHECK(ui.font().glyphWidth == 3);

    ui.beginFrame(mouseAt(Vec2{0.0f, 0.0f}));
    ui.border(Rect{0.0f, 0.0f, 20.0f, 10.0f}, Color{1.0f, 1.0f, 1.0f, 1.0f});
    CHECK(ui.itemCount() == 4);

    ui.beginFrame(mouseAt(Vec2{0.0f, 0.0f}));
    ui.border(Rect{0.0f, 0.0f, 20.0f, 10.0f}, Color{1.0f, 1.0f, 1.0f, 1.0f}, 10.0f);
    CHECK(ui.itemCount() == 2);

    ui.beginFrame(mouseAt(Vec2{0.0f, 0.0f}));
    ui.progressBar(Rect{0.0f, 0.0f, 100.0f, 8.0f}, 50.0f, 0.0f, 100.0f, Color{0.0f, 1.0f, 0.0f, 1.0f});
    CHECK(ui.itemCount() == 2);

    ui.beginFrame(mouseAt(Vec2{0.0f, 0.0f}));
    ui.progressBar(Rect{0.0f, 0.0f, 100.0f, 8.0f}, 0.0f, 0.0f, 100.0f, Color{0.0f, 1.0f, 0.0f, 1.0f});
    CHECK(ui.itemCount() == 1);

    ui.beginFrame(mouseAt(Vec2{0.0f, 0.0f}));
    ui.progressBar(Rect{0.0f, 0.0f, 100.0f, 8.0f}, 500.0f, 0.0f, 100.0f, Color{0.0f, 1.0f, 0.0f, 1.0f});
    CHECK(ui.itemCount() == 2);
    CHECK(ui.font().glyphs.size() == static_cast<std::size_t>(kGlyphCount));
}

TEST_CASE("UI origin offsets draw items but not hit testing", "[ui]") {
    const FontAtlas font = testFont();
    UiContext ui(font, TextureHandle{kWhiteTexture});
    ui.setOrigin(Vec2{100.0f, 200.0f});
    CHECK(ui.origin().x == Approx(100.0f));

    ui.beginFrame(mouseAt(Vec2{5.0f, 5.0f}));
    ui.panel(Rect{0.0f, 0.0f, 10.0f, 10.0f}, Color{1.0f, 1.0f, 1.0f, 1.0f});
    REQUIRE(ui.itemCount() == 1);

    RenderQueue queue;
    ui.submit(queue);
    REQUIRE(queue.size() == 1);
    CHECK(queue.items()[0].position.x == Approx(100.0f));
    CHECK(queue.items()[0].position.y == Approx(200.0f));

    // Mouse coordinates are virtual, so a rect at (0, 0) is hovered even when the
    // origin moved the draw position.
    const UiResult result = ui.button(UiId::fromName("origin"), "", Rect{0.0f, 0.0f, 10.0f, 10.0f});
    CHECK(result.hovered);
}

TEST_CASE("button completes a press on release inside", "[ui]") {
    const FontAtlas font = testFont();
    UiContext ui(font, TextureHandle{kWhiteTexture});
    const UiId id = UiId::fromName("ok");
    const Rect rect{0.0f, 0.0f, 40.0f, 12.0f};

    ui.beginFrame(mouseAt(Vec2{5.0f, 5.0f}, true, true));
    UiResult result = ui.button(id, "OK", rect);
    CHECK(result.hovered);
    CHECK_FALSE(result.pressed);

    ui.beginFrame(mouseAt(Vec2{5.0f, 5.0f}, false, false, true));
    result = ui.button(id, "OK", rect);
    CHECK(result.hovered);
    CHECK(result.pressed);
}

TEST_CASE("button cancels when the release happens outside", "[ui]") {
    const FontAtlas font = testFont();
    UiContext ui(font, TextureHandle{kWhiteTexture});
    const UiId id = UiId::fromName("cancel");
    const Rect rect{0.0f, 0.0f, 40.0f, 12.0f};

    ui.beginFrame(mouseAt(Vec2{5.0f, 5.0f}, true, true));
    static_cast<void>(ui.button(id, "OK", rect));

    ui.beginFrame(mouseAt(Vec2{100.0f, 100.0f}, false, false, true));
    UiResult result = ui.button(id, "OK", rect);
    CHECK_FALSE(result.hovered);
    CHECK_FALSE(result.pressed);

    // The active id was cleared, so a fresh click still works.
    ui.beginFrame(mouseAt(Vec2{5.0f, 5.0f}, true, true));
    static_cast<void>(ui.button(id, "OK", rect));
    ui.beginFrame(mouseAt(Vec2{5.0f, 5.0f}, false, false, true));
    result = ui.button(id, "OK", rect);
    CHECK(result.pressed);
}

TEST_CASE("checkbox toggles once per completed click", "[ui]") {
    const FontAtlas font = testFont();
    UiContext ui(font, TextureHandle{kWhiteTexture});
    const UiId id = UiId::fromName("fullscreen");
    const Rect rect{0.0f, 0.0f, 120.0f, 16.0f};
    bool value = false;

    ui.beginFrame(mouseAt(Vec2{5.0f, 5.0f}, true, true));
    UiResult result = ui.checkbox(id, "Fullscreen", value, rect);
    CHECK_FALSE(result.changed);
    CHECK_FALSE(value);

    ui.beginFrame(mouseAt(Vec2{5.0f, 5.0f}, false, false, true));
    result = ui.checkbox(id, "Fullscreen", value, rect);
    CHECK(result.changed);
    CHECK(result.pressed);
    CHECK(value);

    ui.beginFrame(mouseAt(Vec2{5.0f, 5.0f}, true, true));
    static_cast<void>(ui.checkbox(id, "Fullscreen", value, rect));
    ui.beginFrame(mouseAt(Vec2{5.0f, 5.0f}, false, false, true));
    result = ui.checkbox(id, "Fullscreen", value, rect);
    CHECK(result.changed);
    CHECK_FALSE(value);
}

TEST_CASE("slider tracks the mouse and clamps to its range", "[ui]") {
    const FontAtlas font = testFont();
    UiContext ui(font, TextureHandle{kWhiteTexture});
    const UiId id = UiId::fromName("volume");
    const Rect rect{0.0f, 0.0f, 100.0f, 12.0f};
    float value = 0.0f;

    ui.beginFrame(mouseAt(Vec2{50.0f, 6.0f}, true, true));
    UiResult result = ui.slider(id, value, 0.0f, 1.0f, rect);
    CHECK(result.hovered);
    CHECK(result.changed);
    CHECK(value == Approx(0.5f));

    ui.beginFrame(mouseAt(Vec2{150.0f, 6.0f}, true));
    result = ui.slider(id, value, 0.0f, 1.0f, rect);
    CHECK_FALSE(result.hovered);
    CHECK(result.changed);
    CHECK(value == Approx(1.0f));

    ui.beginFrame(mouseAt(Vec2{-100.0f, 6.0f}, true));
    result = ui.slider(id, value, 0.0f, 1.0f, rect);
    CHECK(result.changed);
    CHECK(value == Approx(0.0f));

    ui.beginFrame(mouseAt(Vec2{20.0f, 6.0f}, false, false, true));
    result = ui.slider(id, value, 0.0f, 1.0f, rect);
    CHECK(result.changed);
    CHECK(value == Approx(0.2f));

    // Once released, moving the mouse no longer drags.
    ui.beginFrame(mouseAt(Vec2{80.0f, 6.0f}));
    result = ui.slider(id, value, 0.0f, 1.0f, rect);
    CHECK_FALSE(result.changed);
    CHECK(value == Approx(0.2f));
}

TEST_CASE("icon submits one textured UI item and ignores unusable input", "[ui]") {
    const FontAtlas font = testFont();
    UiContext ui(font, TextureHandle{kWhiteTexture});
    ui.beginFrame(mouseAt(Vec2{0.0f, 0.0f}));

    ui.icon(Rect{4.0f, 6.0f, 16.0f, 16.0f}, TextureHandle{7}, Color{1.0f, 0.5f, 0.25f, 1.0f});
    REQUIRE(ui.itemCount() == 1);

    RenderQueue queue;
    ui.submit(queue);
    const DrawItem& item = queue.items()[0];
    CHECK(item.texture.value() == 7u);
    CHECK(item.position.x == Approx(4.0f));
    CHECK(item.position.y == Approx(6.0f));
    CHECK(item.size.x == Approx(16.0f));
    CHECK(item.size.y == Approx(16.0f));
    // An empty source rect means "the whole texture", which is what an icon wants.
    CHECK(item.source.w == Approx(0.0f));
    CHECK(item.tint.g == Approx(0.5f));
    CHECK(item.layer == RenderLayer::UI);
    CHECK(item.sortY == Approx(6.0f));

    // Nothing is queued for an invalid handle or a degenerate rect.
    ui.icon(Rect{0.0f, 0.0f, 16.0f, 16.0f}, TextureHandle{});
    ui.icon(Rect{0.0f, 0.0f, 0.0f, 16.0f}, TextureHandle{7});
    CHECK(ui.itemCount() == 1);
}

TEST_CASE("icon follows the UI origin like every other widget", "[ui]") {
    const FontAtlas font = testFont();
    UiContext ui(font, TextureHandle{kWhiteTexture});
    ui.beginFrame(mouseAt(Vec2{0.0f, 0.0f}));
    ui.setOrigin(Vec2{100.0f, 50.0f});
    ui.icon(Rect{4.0f, 6.0f, 16.0f, 16.0f}, TextureHandle{3});

    RenderQueue queue;
    ui.submit(queue);
    REQUIRE(queue.size() == 1);
    CHECK(queue.items()[0].position.x == Approx(104.0f));
    CHECK(queue.items()[0].position.y == Approx(56.0f));
}

TEST_CASE("submit appends every queued item to the caller's queue", "[ui]") {
    const FontAtlas font = testFont();
    UiContext ui(font, TextureHandle{kWhiteTexture});
    ui.beginFrame(mouseAt(Vec2{0.0f, 0.0f}));
    ui.panel(Rect{0.0f, 0.0f, 10.0f, 10.0f}, Color{1.0f, 1.0f, 1.0f, 1.0f});
    ui.panel(Rect{10.0f, 0.0f, 10.0f, 10.0f}, Color{1.0f, 1.0f, 1.0f, 1.0f});
    REQUIRE(ui.itemCount() == 2);

    RenderQueue queue;
    queue.submit(DrawItem{});
    ui.submit(queue);
    CHECK(queue.size() == 3);
    CHECK(queue.items()[1].layer == RenderLayer::UI);
    CHECK(queue.items()[2].layer == RenderLayer::UI);
}

namespace {

/// \brief The built-in font plus two Han glyphs, i.e. what a baked atlas hands to M09.
///
/// The mixed sizes (5x7 Latin next to 10x9 Han) and the per-glyph `bearingY` are the whole
/// point of the extended table, so the fixtures use different bearings on purpose.
FontAtlas mixedFont() {
    FontAtlas font = testFont();
    font.lineHeight = 10.0f;
    const auto add = [&font](char32_t codepoint, float bearingY, float sourceX) {
        Glyph glyph;
        glyph.source = Rect{sourceX, 0.0f, 10.0f, 9.0f};
        glyph.advance = 10.0f;
        glyph.bearingY = bearingY;
        glyph.defined = true;
        font.extended.push_back(fa::ui::ExtendedGlyph{codepoint, glyph});
    };
    add(U'\u00e9', 1.0f, 130.0f);  // é: the first codepoint above the dense ASCII block
    add(U'\u751f', 1.0f, 140.0f);  // 生
    add(U'\u9762', 3.0f, 150.0f);  // 面
    return font;
}

}  // namespace

TEST_CASE("extended glyphs are found by codepoint", "[ui]") {
    const FontAtlas font = mixedFont();

    const Glyph& first = glyphFor(font, U'\u00e9');
    CHECK(first.defined);
    CHECK(first.advance == Approx(10.0f));
    CHECK(first.bearingY == Approx(1.0f));
    CHECK(first.source.x == Approx(130.0f));

    const Glyph& last = glyphFor(font, U'\u9762');
    CHECK(last.defined);
    CHECK(last.bearingY == Approx(3.0f));

    // The dense ASCII block keeps its own indexing next to the extended table.
    CHECK(glyphFor(font, U'A').advance == Approx(static_cast<float>(fa::ui::kGlyphAdvance)));
    CHECK(glyphFor(font, U'A').bearingY == Approx(0.0f));

    // Anything else still falls back to '?'.
    CHECK(glyphFor(font, U'\uff21').source.x == Approx(glyphFor(font, U'?').source.x));
}

TEST_CASE("measureText decodes UTF-8 and adds up per-glyph advances", "[ui]") {
    const FontAtlas font = mixedFont();
    TextStyle style;
    style.scale = 1.0f;

    // Three bytes per Han character, but one codepoint each.
    const TextMetrics han = measureText(font, "\xe7\x94\x9f\xe9\x9d\xa2", 0.0f, style);
    CHECK(han.lines == 1);
    CHECK(han.width == Approx(20.0f));
    CHECK(han.height == Approx(10.0f));

    const TextMetrics mixed = measureText(font, "A\xe7\x94\x9f", 0.0f, style);
    CHECK(mixed.width == Approx(static_cast<float>(fa::ui::kGlyphAdvance) + 10.0f));

    // Wrapping counts too: one 10 px glyph per line inside a 10 px box.
    CHECK(measureText(font, "\xe7\x94\x9f\xe9\x9d\xa2", 10.0f, style).lines == 2);
}

TEST_CASE("drawText tolerates truncated and invalid UTF-8", "[ui]") {
    const FontAtlas font = mixedFont();

    // A lead byte whose continuation bytes are missing must not read past the end; the two
    // stray codepoints still resolve (one is a real extended glyph, one falls back to '?').
    RenderQueue truncated;
    drawText(font, std::string_view("\xe7\x94", 2), Rect{0.0f, 0.0f, 100.0f, 20.0f}, {}, truncated);
    CHECK(truncated.size() == 2);

    RenderQueue stray;
    drawText(font, std::string_view("\xff", 1), Rect{0.0f, 0.0f, 100.0f, 20.0f}, {}, stray);
    CHECK(stray.size() == 1);
}

TEST_CASE("drawText offsets each glyph by its own bearingY", "[ui]") {
    const FontAtlas font = mixedFont();
    RenderQueue queue;

    drawText(font, "\xe7\x94\x9f\xe9\x9d\xa2", Rect{8.0f, 40.0f, 100.0f, 20.0f}, {}, queue);
    const std::span<const DrawItem> items = itemsOf(queue);
    REQUIRE(items.size() == 2);
    CHECK(items[0].position.x == Approx(8.0f));
    CHECK(items[0].position.y == Approx(41.0f));
    CHECK(items[0].size.x == Approx(10.0f));
    CHECK(items[0].size.y == Approx(9.0f));
    CHECK(items[1].position.x == Approx(18.0f));
    CHECK(items[1].position.y == Approx(43.0f));
}

TEST_CASE("a button label is not repainted by its own fill", "[ui]") {
    // Regression: the 10 px atlas at the theme's 2x widget scale is as tall as a 20 px
    // button, so centring put the label on the fill's sortY; the tie then went to the
    // entity id, where a glyph's per-call counter (1) always lost against the context's
    // frame counter (hundreds by the time a menu is drawn), so the opaque button painted
    // its own label out. UI now draws in submission order.
    const FontAtlas font = mixedFont();
    UiContext ui(font, TextureHandle{kWhiteTexture});

    ui.beginFrame(mouseAt(Vec2{500.0f, 500.0f}));
    // Whatever a real frame draws before a menu panel, so the context's id counter is
    // well past the glyph ids drawText() assigns per call.
    ui.panel(Rect{0.0f, 0.0f, 300.0f, 246.0f}, Color{0.1f, 0.1f, 0.1f, 1.0f});
    ui.panel(Rect{0.0f, 0.0f, 300.0f, 246.0f}, Color{0.1f, 0.1f, 0.1f, 1.0f});
    static_cast<void>(
        ui.button(UiId::fromName("menu.new"), "\xe7\x94\x9f\xe9\x9d\xa2", Rect{0.0f, 100.0f, 160.0f, 20.0f}));

    RenderQueue queue;
    ui.submit(queue);
    queue.sort();
    const std::span<const DrawItem> items = itemsOf(queue);

    int fill = -1;
    int label = -1;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].size.x == Approx(160.0f) && items[i].size.y == Approx(20.0f)) {
            fill = static_cast<int>(i);
        } else if (items[i].size.x == Approx(20.0f) && items[i].size.y == Approx(18.0f) && label < 0) {
            label = static_cast<int>(i);
        }
    }
    REQUIRE(fill >= 0);
    REQUIRE(label >= 0);
    CHECK(label > fill);
}

TEST_CASE("fitToCodepoints cuts on codepoint boundaries", "[ui]") {
    constexpr std::string_view han{"\xe7\x94\x9f\xe9\x9d\xa2", 6};  // 2 characters, 6 bytes
    CHECK(fa::ui::fitToCodepoints(han, 1) == std::string_view{"\xe7\x94\x9f", 3});
    CHECK(fa::ui::fitToCodepoints(han, 2) == han);
    CHECK(fa::ui::fitToCodepoints(han, 9) == han);
    CHECK(fa::ui::fitToCodepoints({}, 4).empty());

    // A truncated sequence is still one "character", so the helper never runs off the end.
    constexpr std::string_view cut{"\xe7\x94", 2};
    CHECK(fa::ui::fitToCodepoints(cut, 1).size() == 1);
    CHECK(fa::ui::fitToCodepoints(cut, 2).size() == 2);
}
