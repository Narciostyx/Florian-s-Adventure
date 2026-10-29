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
