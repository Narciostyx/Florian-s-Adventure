#pragma once

#include <glm/glm.hpp>

namespace fa::core {

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kTau = 2.0f * kPi;

/// World units are logical pixels (docs/architecture/pixel-metrics.md), y points
/// down, so `y + h` is always "lower on screen".
using Vec2 = glm::vec2;

/// Normalized rgba in `[0, 1]`; not premultiplied.
using Color = glm::vec4;

/// Axis-aligned rectangle stored as top-left corner + size. Deliberately not a
/// glm::vec4: `rect.w` / `rect.h` carries meaning that `rect.z` / `rect.w` does not,
/// and collision code reads it constantly. See docs/adr/0004-core-math-and-result-shape.md.
struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

constexpr Rect makeRect(float x, float y, float w, float h) noexcept {
    return Rect{x, y, w, h};
}

constexpr Rect rectFromCenter(Vec2 center, float w, float h) noexcept {
    return Rect{center.x - w * 0.5f, center.y - h * 0.5f, w, h};
}

constexpr float left(const Rect& r) noexcept {
    return r.x;
}
constexpr float right(const Rect& r) noexcept {
    return r.x + r.w;
}
constexpr float top(const Rect& r) noexcept {
    return r.y;
}
constexpr float bottom(const Rect& r) noexcept {
    return r.y + r.h;
}

constexpr Vec2 center(const Rect& r) noexcept {
    return Vec2{r.x + r.w * 0.5f, r.y + r.h * 0.5f};
}

constexpr bool isEmpty(const Rect& r) noexcept {
    return r.w <= 0.0f || r.h <= 0.0f;
}

/// Half-open on both axes: touching edges do not overlap. That is what tile and
/// trigger queries want, and it makes `rectOverlaps(a, a)` trivially true.
constexpr bool rectOverlaps(const Rect& a, const Rect& b) noexcept {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

constexpr bool rectContains(const Rect& r, Vec2 point) noexcept {
    return point.x >= r.x && point.x < r.x + r.w && point.y >= r.y && point.y < r.y + r.h;
}

constexpr bool rectContainsRect(const Rect& outer, const Rect& inner) noexcept {
    return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.w <= outer.x + outer.w &&
           inner.y + inner.h <= outer.y + outer.h;
}

/// Empty Rect (w == h == 0) when the inputs do not overlap.
constexpr Rect intersect(const Rect& a, const Rect& b) noexcept {
    const float x0 = a.x > b.x ? a.x : b.x;
    const float y0 = a.y > b.y ? a.y : b.y;
    const float x1 = right(a) < right(b) ? right(a) : right(b);
    const float y1 = bottom(a) < bottom(b) ? bottom(a) : bottom(b);
    if (x1 <= x0 || y1 <= y0) {
        return Rect{};
    }
    return Rect{x0, y0, x1 - x0, y1 - y0};
}

constexpr Rect inset(const Rect& r, float dx, float dy) noexcept {
    return Rect{r.x + dx, r.y + dy, r.w - dx * 2.0f, r.h - dy * 2.0f};
}

constexpr float lerp(float a, float b, float t) noexcept {
    return a + (b - a) * t;
}

constexpr Vec2 lerp(Vec2 a, Vec2 b, float t) noexcept {
    return Vec2{fa::core::lerp(a.x, b.x, t), fa::core::lerp(a.y, b.y, t)};
}

constexpr Color lerp(Color a, Color b, float t) noexcept {
    return Color{fa::core::lerp(a.r, b.r, t),
                 fa::core::lerp(a.g, b.g, t),
                 fa::core::lerp(a.b, b.b, t),
                 fa::core::lerp(a.a, b.a, t)};
}

constexpr float clampf(float value, float lo, float hi) noexcept {
    return value < lo ? lo : (value > hi ? hi : value);
}

constexpr float clamp01(float value) noexcept {
    return clampf(value, 0.0f, 1.0f);
}

/// Moves `current` toward `target` by at most `maxDelta`. Frame-rate independent
/// for velocity/damping code where a plain lerp would drift with dt.
inline float approach(float current, float target, float maxDelta) noexcept {
    const float delta = target - current;
    if (delta > maxDelta) {
        return current + maxDelta;
    }
    if (delta < -maxDelta) {
        return current - maxDelta;
    }
    return target;
}

/// Pixel-art note: snap only at draw time (camera transform), never in simulation
/// state, or sub-pixel movement speed becomes impossible.
inline float snapToPixel(float value) noexcept {
    return static_cast<float>(static_cast<int>(value < 0.0f ? value - 0.5f : value + 0.5f));
}

}  // namespace fa::core
