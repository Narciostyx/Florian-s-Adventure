/// \file
/// \brief Tests for M06: draw items and sort order, the render queue batching
/// cost, the integer-scaled viewport and the camera. Everything here runs
/// without an OpenGL context; the GL renderer itself is covered by the app smoke
/// run described in docs/build/m06-render.md.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <fa/core/Math.hpp>
#include <fa/render/Camera2D.hpp>
#include <fa/render/RenderConstants.hpp>
#include <fa/render/RenderQueue.hpp>
#include <fa/render/RenderTypes.hpp>
#include <fa/render/Viewport.hpp>

namespace {

using Catch::Approx;
using fa::core::Color;
using fa::core::makeRect;
using fa::core::Rect;
using fa::core::Vec2;
using fa::render::bytesPerPixel;
using fa::render::Camera2D;
using fa::render::depthKey;
using fa::render::DrawItem;
using fa::render::drawsBefore;
using fa::render::RenderLayer;
using fa::render::RenderQueue;
using fa::render::TextureDesc;
using fa::render::TextureFormat;
using fa::render::TextureHandle;
using fa::render::toString;
using fa::render::Viewport;

/// Tolerance for float comparisons; every calculation below is exact in binary
/// floating point, so this only guards against future refactors.
constexpr float kEpsilon = 1.0e-5f;

DrawItem makeItem(TextureHandle texture, RenderLayer layer, float sortY, std::uint32_t entityId = 0) {
    DrawItem item;
    item.texture = texture;
    item.layer = layer;
    item.sortY = sortY;
    item.entityId = entityId;
    return item;
}

std::vector<TextureHandle> makeTextures() {
    return {TextureHandle{1}, TextureHandle{2}, TextureHandle{3}};
}

}  // namespace

TEST_CASE("TextureHandle defaults to invalid", "[render]") {
    const TextureHandle handle;
    REQUIRE_FALSE(handle.isValid());
    REQUIRE(handle.value() == 0u);
}

TEST_CASE("TextureHandle carries its index and compares by value", "[render]") {
    const TextureHandle one{1};
    const TextureHandle other{1};
    const TextureHandle two{2};

    REQUIRE(one.isValid());
    REQUIRE(one.value() == 1u);
    REQUIRE(one == other);
    REQUIRE(one != two);
    REQUIRE(one < two);
}

TEST_CASE("bytesPerPixel reports the layout of every format", "[render]") {
    REQUIRE(bytesPerPixel(TextureFormat::Rgba8) == 4);
    REQUIRE(bytesPerPixel(TextureFormat::R8) == 1);
    REQUIRE(bytesPerPixel(TextureFormat::Count) == 0);
}

TEST_CASE("TextureFormat has a name for each real value", "[render]") {
    REQUIRE(toString(TextureFormat::Rgba8) == "Rgba8");
    REQUIRE(toString(TextureFormat::R8) == "R8");
    // Count is a sentinel, not a format, so it reports like any other unknown value.
    REQUIRE(toString(TextureFormat::Count) == "Unknown");
}

TEST_CASE("TextureDesc derives the row pitch it reads with", "[render]") {
    TextureDesc packed;
    packed.width = 16;
    packed.height = 8;
    REQUIRE(packed.effectiveRowPitch() == 64);

    packed.format = TextureFormat::R8;
    REQUIRE(packed.effectiveRowPitch() == 16);

    TextureDesc padded;
    padded.width = 16;
    padded.height = 8;
    padded.rowPitchBytes = 80;
    REQUIRE(padded.effectiveRowPitch() == 80);
}

TEST_CASE("TextureDesc returns the requested row pitch verbatim", "[render]") {
    TextureDesc desc;
    desc.width = 16;
    desc.rowPitchBytes = 4;
    // Below one row, so no valid read exists; the renderer rejects such an
    // upload instead of guessing, and the pitch is reported as asked for.
    REQUIRE(desc.effectiveRowPitch() == 4);
}

TEST_CASE("RenderLayer names every layer", "[render]") {
    REQUIRE(toString(RenderLayer::Background) == "Background");
    REQUIRE(toString(RenderLayer::Ground) == "Ground");
    REQUIRE(toString(RenderLayer::GroundDetail) == "GroundDetail");
    REQUIRE(toString(RenderLayer::Objects) == "Objects");
    REQUIRE(toString(RenderLayer::Actors) == "Actors");
    REQUIRE(toString(RenderLayer::Foreground) == "Foreground");
    REQUIRE(toString(RenderLayer::Weather) == "Weather");
    REQUIRE(toString(RenderLayer::UI) == "UI");
    REQUIRE(toString(RenderLayer::Count) == "Unknown");
}

TEST_CASE("RenderLayer order is the draw order", "[render]") {
    const RenderLayer order[] = {RenderLayer::Background,
                                 RenderLayer::Ground,
                                 RenderLayer::GroundDetail,
                                 RenderLayer::Objects,
                                 RenderLayer::Actors,
                                 RenderLayer::Foreground,
                                 RenderLayer::Weather,
                                 RenderLayer::UI};
    constexpr std::size_t count = sizeof(order) / sizeof(order[0]);
    REQUIRE(count == static_cast<std::size_t>(RenderLayer::Count));

    for (std::size_t i = 1; i < count; ++i) {
        REQUIRE(static_cast<int>(order[i - 1]) < static_cast<int>(order[i]));
    }
}

TEST_CASE("depthKey rounds a sort position to whole pixels", "[render]") {
    REQUIRE(depthKey(0.0f) == 0);
    REQUIRE(depthKey(10.4f) == 10);
    REQUIRE(depthKey(10.5f) == 11);
    REQUIRE(depthKey(10.6f) == 11);
    REQUIRE(depthKey(-10.4f) == -10);
    REQUIRE(depthKey(-10.6f) == -11);
    REQUIRE(depthKey(-0.6f) == -1);
}

TEST_CASE("depthKey keeps two actors on the same row interchangeable", "[render]") {
    // Same row, 0.2 pixels apart: without quantisation these would swap order
    // every frame and flicker.
    REQUIRE(depthKey(100.1f) == depthKey(100.3f));
}

TEST_CASE("drawsBefore orders by layer first", "[render]") {
    const std::vector<TextureHandle> textures = makeTextures();
    const DrawItem background = makeItem(textures[0], RenderLayer::Background, 1000.0f);
    const DrawItem actor = makeItem(textures[0], RenderLayer::Actors, 0.0f);

    REQUIRE(drawsBefore(background, actor));
    REQUIRE_FALSE(drawsBefore(actor, background));
}

TEST_CASE("drawsBefore orders by depth inside a layer", "[render]") {
    const std::vector<TextureHandle> textures = makeTextures();
    const DrawItem above = makeItem(textures[0], RenderLayer::Actors, 50.0f, 1);
    const DrawItem below = makeItem(textures[0], RenderLayer::Actors, 60.0f, 2);

    REQUIRE(drawsBefore(above, below));
    REQUIRE_FALSE(drawsBefore(below, above));
}

TEST_CASE("drawsBefore breaks depth ties with the entity id", "[render]") {
    const std::vector<TextureHandle> textures = makeTextures();
    const DrawItem first = makeItem(textures[0], RenderLayer::Actors, 50.0f, 7);
    const DrawItem second = makeItem(textures[0], RenderLayer::Actors, 50.0f, 9);

    REQUIRE(drawsBefore(first, second));
    REQUIRE_FALSE(drawsBefore(second, first));
}

TEST_CASE("drawsBefore is not a strict order for identical keys", "[render]") {
    const std::vector<TextureHandle> textures = makeTextures();
    const DrawItem left = makeItem(textures[0], RenderLayer::Objects, 10.0f, 3);
    const DrawItem right = makeItem(textures[1], RenderLayer::Objects, 10.0f, 3);

    REQUIRE_FALSE(drawsBefore(left, right));
    REQUIRE_FALSE(drawsBefore(right, left));
}

TEST_CASE("RenderQueue starts empty and grows on submit", "[render]") {
    RenderQueue queue;
    REQUIRE(queue.empty());
    REQUIRE(queue.size() == 0);

    const std::vector<TextureHandle> textures = makeTextures();
    queue.submit(makeItem(textures[0], RenderLayer::Ground, 0.0f));
    queue.submit(makeItem(textures[0], RenderLayer::Ground, 0.0f));
    REQUIRE(queue.size() == 2);

    queue.clear();
    REQUIRE(queue.empty());
    REQUIRE(queue.items().empty());
}

TEST_CASE("RenderQueue reserves capacity without changing contents", "[render]") {
    RenderQueue queue;
    queue.reserve(64);
    REQUIRE(queue.size() == 0);

    const std::vector<TextureHandle> textures = makeTextures();
    queue.submit(makeItem(textures[0], RenderLayer::Ground, 1.0f));
    REQUIRE(queue.size() == 1);
}

TEST_CASE("RenderQueue sorts by layer, depth and entity id", "[render]") {
    const std::vector<TextureHandle> textures = makeTextures();

    RenderQueue queue;
    queue.submit(makeItem(textures[0], RenderLayer::UI, 0.0f, 1));
    queue.submit(makeItem(textures[0], RenderLayer::Actors, 80.0f, 2));
    queue.submit(makeItem(textures[0], RenderLayer::Actors, 20.0f, 3));
    queue.submit(makeItem(textures[0], RenderLayer::Ground, 999.0f, 4));
    queue.sort();

    const auto items = queue.items();
    REQUIRE(items.size() == 4);
    REQUIRE(items[0].layer == RenderLayer::Ground);
    REQUIRE(items[1].entityId == 3u);
    REQUIRE(items[2].entityId == 2u);
    REQUIRE(items[3].layer == RenderLayer::UI);
}

TEST_CASE("RenderQueue keeps submission order for equal keys", "[render]") {
    const std::vector<TextureHandle> textures = makeTextures();

    RenderQueue queue;
    queue.submit(makeItem(textures[1], RenderLayer::Actors, 40.0f, 5));
    queue.submit(makeItem(textures[2], RenderLayer::Actors, 40.0f, 5));
    queue.submit(makeItem(textures[0], RenderLayer::Actors, 40.0f, 5));
    queue.submit(makeItem(textures[1], RenderLayer::Actors, 40.0f, 1));
    queue.sort();

    const auto items = queue.items();
    REQUIRE(items.size() == 4);
    // entityId 1 has the smaller key, so it moves to the front; the three items
    // that compare equal keep their submission order and textures.
    REQUIRE(items[0].entityId == 1u);
    REQUIRE(items[1].texture == textures[1]);
    REQUIRE(items[2].texture == textures[2]);
    REQUIRE(items[3].texture == textures[0]);
}

TEST_CASE("countDrawCalls is zero for an empty frame", "[render]") {
    const std::vector<DrawItem> items;
    REQUIRE(RenderQueue::countDrawCalls(items) == 0);
    REQUIRE(RenderQueue::countTextureSwitches(items) == 0);
}

TEST_CASE("countDrawCalls counts runs of one texture as one call", "[render]") {
    const std::vector<TextureHandle> textures = makeTextures();
    const std::vector<DrawItem> items = {
        makeItem(textures[0], RenderLayer::Ground, 0.0f),
        makeItem(textures[0], RenderLayer::Ground, 0.0f),
        makeItem(textures[0], RenderLayer::Ground, 0.0f),
    };

    REQUIRE(RenderQueue::countDrawCalls(items) == 1);
    REQUIRE(RenderQueue::countTextureSwitches(items) == 0);
}

TEST_CASE("countDrawCalls counts every texture change", "[render]") {
    const std::vector<TextureHandle> textures = makeTextures();
    const std::vector<DrawItem> items = {
        makeItem(textures[0], RenderLayer::Ground, 0.0f),
        makeItem(textures[1], RenderLayer::Ground, 0.0f),
        makeItem(textures[0], RenderLayer::Ground, 0.0f),
    };

    REQUIRE(RenderQueue::countDrawCalls(items) == 3);
    REQUIRE(RenderQueue::countTextureSwitches(items) == 2);
}

TEST_CASE("countDrawCalls trusts the order it is given", "[render]") {
    const std::vector<TextureHandle> textures = makeTextures();
    RenderQueue queue;
    queue.submit(makeItem(textures[0], RenderLayer::Actors, 10.0f));
    queue.submit(makeItem(textures[1], RenderLayer::Actors, 20.0f));

    // Unsorted submission order is also the order the queue would draw in, so
    // the two-texture cost is reported before sorting as well.
    REQUIRE(RenderQueue::countDrawCalls(queue.items()) == 2);

    queue.sort();
    REQUIRE(RenderQueue::countDrawCalls(queue.items()) == 2);
}

TEST_CASE("Viewport fits a 1280x720 window at exactly double scale", "[render]") {
    const Viewport viewport = Viewport::fit(1280, 720);

    REQUIRE(viewport.scale == 2);
    REQUIRE(viewport.offsetX == 0);
    REQUIRE(viewport.offsetY == 0);
    REQUIRE(viewport.drawableWidth() == 1280);
    REQUIRE(viewport.drawableHeight() == 720);
    REQUIRE_FALSE(viewport.isEmpty());
}

TEST_CASE("Viewport letterboxes a window that is not a whole multiple", "[render]") {
    const Viewport viewport = Viewport::fit(1280, 700);

    REQUIRE(viewport.scale == 1);
    REQUIRE(viewport.offsetX == 320);
    REQUIRE(viewport.offsetY == 170);
    REQUIRE(viewport.drawableWidth() == 640);
    REQUIRE(viewport.windowRect().h == 360.0f);
}

TEST_CASE("Viewport crops instead of shrinking below 1:1", "[render]") {
    const Viewport viewport = Viewport::fit(320, 180);

    REQUIRE(viewport.scale == 1);
    REQUIRE(viewport.offsetX == -160);
    REQUIRE(viewport.offsetY == -90);
    REQUIRE(viewport.drawableWidth() == 640);
}

TEST_CASE("Viewport of a zero-sized window is empty", "[render]") {
    const Viewport viewport = Viewport::fit(0, 0);

    REQUIRE(viewport.isEmpty());
    REQUIRE(viewport.scale == 1);
    REQUIRE(viewport.drawableWidth() == 640);
    REQUIRE(viewport.drawableHeight() == 360);
}

TEST_CASE("Viewport falls back to the virtual surface size for bad arguments", "[render]") {
    const Viewport viewport = Viewport::fit(1280, 720, 0, -1);

    REQUIRE(viewport.virtualWidth == 640);
    REQUIRE(viewport.virtualHeight == 360);
    REQUIRE(viewport.scale == 2);
}

TEST_CASE("Viewport maps window pixels to virtual pixels and back", "[render]") {
    const Viewport viewport = Viewport::fit(1280, 700);
    REQUIRE(viewport == Viewport::fit(1280, 700));

    const Vec2 virtualPoint = viewport.windowToVirtual(Vec2{320.0f + 64.0f, 170.0f + 32.0f});
    REQUIRE(std::fabs(virtualPoint.x - 64.0f) < kEpsilon);
    REQUIRE(std::fabs(virtualPoint.y - 32.0f) < kEpsilon);

    const Vec2 windowPoint = viewport.virtualToWindow(virtualPoint);
    REQUIRE(std::fabs(windowPoint.x - 384.0f) < kEpsilon);
    REQUIRE(std::fabs(windowPoint.y - 202.0f) < kEpsilon);
}

TEST_CASE("Viewport windowRect mirrors the offsets and scale", "[render]") {
    const Viewport viewport = Viewport::fit(1280, 700);
    const Rect rect = viewport.windowRect();

    REQUIRE_FALSE(fa::core::isEmpty(rect));
    REQUIRE(rect.x == 320.0f);
    REQUIRE(rect.y == 170.0f);
    REQUIRE(rect.w == 640.0f);
    REQUIRE(rect.h == 360.0f);
}

TEST_CASE("Camera2D defaults to the centre of a 640x360 surface", "[render]") {
    const Camera2D camera;

    REQUIRE(camera.viewportSize().x == 640.0f);
    REQUIRE(camera.viewportSize().y == 360.0f);
    REQUIRE_FALSE(camera.hasBounds());

    const Rect visible = camera.visibleRect();
    REQUIRE(visible.x == -320.0f);
    REQUIRE(visible.y == -180.0f);
    REQUIRE(visible.w == 640.0f);
    REQUIRE(visible.h == 360.0f);
}

TEST_CASE("Camera2D shows its position at the centre of the view", "[render]") {
    Camera2D camera;
    camera.setPosition(Vec2{320.0f, 180.0f});

    const Rect visible = camera.visibleRect();
    REQUIRE(visible.x == 0.0f);
    REQUIRE(visible.y == 0.0f);
    REQUIRE(visible.w == 640.0f);
    REQUIRE(visible.h == 360.0f);

    const Vec2 centre = camera.worldToViewport(Vec2{320.0f, 180.0f});
    REQUIRE(std::fabs(centre.x - 320.0f) < kEpsilon);
    REQUIRE(std::fabs(centre.y - 180.0f) < kEpsilon);

    const Vec2 topLeft = camera.worldToViewport(Vec2{0.0f, 0.0f});
    REQUIRE(std::fabs(topLeft.x) < kEpsilon);
    REQUIRE(std::fabs(topLeft.y) < kEpsilon);
}

TEST_CASE("Camera2D round-trips world and viewport coordinates", "[render]") {
    Camera2D camera;
    camera.setPosition(Vec2{123.0f, 456.0f});

    const Vec2 world{200.0f, 300.0f};
    const Vec2 back = camera.viewportToWorld(camera.worldToViewport(world));
    REQUIRE(std::fabs(back.x - world.x) < kEpsilon);
    REQUIRE(std::fabs(back.y - world.y) < kEpsilon);
}

TEST_CASE("Camera2D worldToPixel rounds to whole virtual pixels", "[render]") {
    Camera2D camera;
    camera.setPosition(Vec2{0.5f, 0.5f});

    // A world point on a half pixel lands on a quarter pixel of the viewport.
    const Vec2 pixel = camera.worldToPixel(Vec2{0.0f, 0.0f});
    REQUIRE(pixel.x == 320.0f);
    REQUIRE(pixel.y == 180.0f);

    const Vec2 rounded = camera.worldToPixel(Vec2{0.4f, 0.4f});
    REQUIRE(rounded.x == 320.0f);
    REQUIRE(rounded.y == 180.0f);

    const Vec2 roundedUp = camera.worldToPixel(Vec2{0.7f, 0.7f});
    REQUIRE(roundedUp.x == 320.0f);
    REQUIRE(roundedUp.y == 180.0f);
}

TEST_CASE("Camera2D snapped aligns the view to whole pixels", "[render]") {
    Camera2D camera;
    camera.setPosition(Vec2{10.4f, 20.6f});

    const Camera2D snapped = camera.snapped();
    REQUIRE(snapped.position().x == 10.0f);
    REQUIRE(snapped.position().y == 21.0f);
    // The original is untouched: simulation code keeps its sub-pixel position.
    REQUIRE(camera.position().x == 10.4f);
}

TEST_CASE("Camera2D projection maps the visible rect onto clip space", "[render]") {
    Camera2D camera;
    camera.setPosition(Vec2{320.0f, 180.0f});

    float matrix[16]{};
    camera.projection(matrix);

    const Vec2 topLeft{0.0f, 0.0f};
    const Vec2 x0 = Vec2{matrix[0] * topLeft.x + matrix[4] * topLeft.y + matrix[12],
                         matrix[1] * topLeft.x + matrix[5] * topLeft.y + matrix[13]};
    REQUIRE(std::fabs(x0.x - (-1.0f)) < kEpsilon);
    REQUIRE(std::fabs(x0.y - 1.0f) < kEpsilon);

    const Vec2 bottomRight{640.0f, 360.0f};
    const Vec2 x1 = Vec2{matrix[0] * bottomRight.x + matrix[4] * bottomRight.y + matrix[12],
                         matrix[1] * bottomRight.x + matrix[5] * bottomRight.y + matrix[13]};
    REQUIRE(std::fabs(x1.x - 1.0f) < kEpsilon);
    REQUIRE(std::fabs(x1.y - (-1.0f)) < kEpsilon);
}

TEST_CASE("Camera2D clamps to bounds larger than the viewport", "[render]") {
    Camera2D camera;
    camera.setBounds(makeRect(0.0f, 0.0f, 1280.0f, 720.0f));

    camera.setPosition(Vec2{-500.0f, -500.0f});
    REQUIRE(camera.position().x == 320.0f);
    REQUIRE(camera.position().y == 180.0f);

    camera.setPosition(Vec2{5000.0f, 5000.0f});
    REQUIRE(camera.position().x == 960.0f);
    REQUIRE(camera.position().y == 540.0f);

    // Halfway along each axis stays where it was asked to be.
    camera.setPosition(Vec2{640.0f, 360.0f});
    REQUIRE(camera.position().x == 640.0f);
    REQUIRE(camera.position().y == 360.0f);
}

TEST_CASE("Camera2D centres bounds smaller than the viewport", "[render]") {
    Camera2D camera;
    camera.setBounds(makeRect(100.0f, 100.0f, 200.0f, 100.0f));

    REQUIRE(camera.position().x == 200.0f);
    REQUIRE(camera.position().y == 150.0f);

    camera.setPosition(Vec2{0.0f, 0.0f});
    REQUIRE(camera.position().x == 200.0f);
    REQUIRE(camera.position().y == 150.0f);
}

TEST_CASE("Camera2D move applies the same clamp as setPosition", "[render]") {
    Camera2D camera;
    camera.setBounds(makeRect(0.0f, 0.0f, 1000.0f, 1000.0f));
    camera.setPosition(Vec2{500.0f, 500.0f});

    camera.move(Vec2{10.0f, -10.0f});
    REQUIRE(camera.position().x == 510.0f);
    REQUIRE(camera.position().y == 490.0f);

    camera.move(Vec2{10000.0f, 0.0f});
    REQUIRE(camera.position().x == 680.0f);
}

TEST_CASE("Camera2D clearBounds releases the clamp", "[render]") {
    Camera2D camera;
    camera.setBounds(makeRect(0.0f, 0.0f, 640.0f, 360.0f));
    REQUIRE(camera.hasBounds());

    camera.clearBounds();
    REQUIRE_FALSE(camera.hasBounds());

    camera.setPosition(Vec2{-999.0f, -999.0f});
    REQUIRE(camera.position().x == -999.0f);
    REQUIRE(camera.position().y == -999.0f);
}

TEST_CASE("Camera2D rejects a degenerate viewport size", "[render]") {
    Camera2D camera;
    camera.setViewportSize(Vec2{0.0f, 0.0f});
    REQUIRE(camera.viewportSize().x == 640.0f);
    REQUIRE(camera.viewportSize().y == 360.0f);

    camera.setViewportSize(Vec2{320.0f, 180.0f});
    REQUIRE(camera.viewportSize().x == 320.0f);
    REQUIRE(camera.viewportSize().y == 180.0f);

    const Rect visible = camera.visibleRect();
    REQUIRE(visible.w == 320.0f);
    REQUIRE(visible.h == 180.0f);
}

TEST_CASE("Camera2D follows a character across a 128x128 map", "[render][camera]") {
    // 128 tiles at 16 px = a 2048x2048 world. The 640x360 viewport is smaller,
    // so the camera can actually move instead of being pinned by the bounds.
    const Rect map = makeRect(0.0f, 0.0f, 128.0f * fa::render::kTileSize, 128.0f * fa::render::kTileSize);
    Camera2D camera;
    camera.setBounds(map);
    camera.setPosition(Vec2{320.0f, 180.0f});

    fa::render::CameraFollowConfig config;
    config.deadzone = Vec2{0.0f, 0.0f};
    config.smoothing = 0.0f;
    camera.setFollowConfig(config);

    // Walking one tile at a time keeps the target at the centre.
    Vec2 target{320.0f, 180.0f};
    for (int step = 0; step < 48; ++step) {
        target.x += static_cast<float>(fa::render::kTileSize);
        target.y += static_cast<float>(fa::render::kTileSize);
        camera.follow(target, 1.0f / 60.0f);
        REQUIRE(camera.position().x == target.x);
        REQUIRE(camera.position().y == target.y);
        const Rect visible = camera.visibleRect();
        REQUIRE(visible.x >= map.x);
        REQUIRE(visible.y >= map.y);
        REQUIRE(fa::core::right(visible) <= fa::core::right(map));
        REQUIRE(fa::core::bottom(visible) <= fa::core::bottom(map));
    }

    // Following past the top-left or bottom-right corner clamps to the map.
    camera.follow(Vec2{-500.0f, -500.0f}, 1.0f / 60.0f);
    REQUIRE(camera.position().x == 320.0f);
    REQUIRE(camera.position().y == 180.0f);

    camera.follow(Vec2{5000.0f, 5000.0f}, 1.0f / 60.0f);
    REQUIRE(camera.position().x == 1728.0f);
    REQUIRE(camera.position().y == 1868.0f);
    REQUIRE(fa::core::right(camera.visibleRect()) == fa::core::right(map));
    REQUIRE(fa::core::bottom(camera.visibleRect()) == fa::core::bottom(map));
}

TEST_CASE("Camera2D deadzone ignores small target motion", "[render][camera]") {
    Camera2D camera;
    camera.setPosition(Vec2{1000.0f, 1000.0f});

    fa::render::CameraFollowConfig config;
    config.deadzone = Vec2{32.0f, 24.0f};
    config.smoothing = 0.0f;
    camera.setFollowConfig(config);

    camera.follow(Vec2{1020.0f, 1010.0f}, 1.0f / 60.0f);
    REQUIRE(camera.position().x == 1000.0f);
    REQUIRE(camera.position().y == 1000.0f);

    // Leaving the horizontal edge places the target on that edge.
    camera.follow(Vec2{1040.0f, 1000.0f}, 1.0f / 60.0f);
    REQUIRE(camera.position().x == 1008.0f);
    REQUIRE(camera.position().y == 1000.0f);

    // Leaving the vertical edge does the same for y.
    camera.follow(Vec2{1008.0f, 1040.0f}, 1.0f / 60.0f);
    REQUIRE(camera.position().x == 1008.0f);
    REQUIRE(camera.position().y == 1016.0f);
}

TEST_CASE("Camera2D follow smoothing eases toward the target", "[render][camera]") {
    Camera2D camera;
    camera.setPosition(Vec2{0.0f, 0.0f});

    fa::render::CameraFollowConfig config;
    config.deadzone = Vec2{0.0f, 0.0f};
    config.smoothing = 10.0f;
    camera.setFollowConfig(config);

    camera.follow(Vec2{100.0f, 0.0f}, 0.1f);
    const float first = camera.position().x;
    CHECK(first > 0.0f);
    CHECK(first < 100.0f);
    CHECK(first == Approx(63.2121f).margin(0.01f));

    camera.follow(Vec2{100.0f, 0.0f}, 0.1f);
    CHECK(camera.position().x > first);
    CHECK(camera.position().x < 100.0f);
}

TEST_CASE("Camera2D follow respects a maximum speed", "[render][camera]") {
    Camera2D camera;
    camera.setPosition(Vec2{0.0f, 0.0f});

    fa::render::CameraFollowConfig config;
    config.deadzone = Vec2{0.0f, 0.0f};
    config.smoothing = 0.0f;
    config.maxSpeed = 200.0f;
    camera.setFollowConfig(config);

    camera.follow(Vec2{1000.0f, 0.0f}, 0.1f);
    CHECK(camera.position().x == Approx(20.0f).margin(0.001f));
    camera.follow(Vec2{1000.0f, 0.0f}, 0.1f);
    CHECK(camera.position().x == Approx(40.0f).margin(0.001f));
}

TEST_CASE("Camera2D rejects negative follow tuning", "[render][camera]") {
    Camera2D camera;
    fa::render::CameraFollowConfig config;
    config.deadzone = Vec2{-10.0f, -20.0f};
    config.smoothing = -1.0f;
    config.maxSpeed = -5.0f;
    camera.setFollowConfig(config);

    CHECK(camera.followConfig().deadzone.x == 0.0f);
    CHECK(camera.followConfig().deadzone.y == 0.0f);
    CHECK(camera.followConfig().smoothing == 0.0f);
    CHECK(camera.followConfig().maxSpeed == 0.0f);

    camera.setPosition(Vec2{5.0f, 5.0f});
    camera.follow(Vec2{10.0f, 10.0f}, 1.0f / 60.0f);
    CHECK(camera.position().x == 10.0f);
    CHECK(camera.position().y == 10.0f);
}

TEST_CASE("DrawItem defaults to an untextured white objects quad", "[render]") {
    const DrawItem item;

    REQUIRE_FALSE(item.texture.isValid());
    REQUIRE(item.size.x == 1.0f);
    REQUIRE(item.size.y == 1.0f);
    REQUIRE(fa::core::isEmpty(item.source));
    REQUIRE(item.tint.r == 1.0f);
    REQUIRE(item.tint.g == 1.0f);
    REQUIRE(item.tint.b == 1.0f);
    REQUIRE(item.tint.a == 1.0f);
    REQUIRE(item.rotation == 0.0f);
    REQUIRE_FALSE(item.flipX);
    REQUIRE_FALSE(item.flipY);
    REQUIRE(item.layer == RenderLayer::Objects);
    REQUIRE(item.entityId == 0u);
}

TEST_CASE("RenderConstants describe a 640x360 16-pixel-tile surface", "[render]") {
    REQUIRE(fa::render::kVirtualWidth == 640);
    REQUIRE(fa::render::kVirtualHeight == 360);
    REQUIRE(fa::render::kTileSize == 16);
    // 40 whole tiles across, 22.5 down: 16:9 is not a multiple of 16, so the
    // bottom row is always half visible. M10 has to keep that in mind when it
    // sizes a screen of tiles.
    REQUIRE(fa::render::kVirtualWidth % fa::render::kTileSize == 0);
    REQUIRE(fa::render::kVirtualHeight / fa::render::kTileSize == 22);
    REQUIRE(fa::render::kVirtualHeight % fa::render::kTileSize == 8);
    REQUIRE(fa::render::kDefaultMaxQuads > 0);
    REQUIRE(fa::render::kMaxTextures > 0);
    REQUIRE(fa::render::kMaxTextureSize >= 4096);
}
