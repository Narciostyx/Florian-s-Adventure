#include "fa/anim/AnimationClip.hpp"

#include <cmath>

namespace fa::anim {

Direction directionFromVector(core::Vec2 move, Direction fallback) noexcept {
    const float horizontal = std::abs(move.x);
    const float vertical = std::abs(move.y);
    if (horizontal <= 0.0f && vertical <= 0.0f) {
        return fallback;
    }
    if (horizontal >= vertical) {
        return move.x < 0.0f ? Direction::Left : Direction::Right;
    }
    return move.y < 0.0f ? Direction::Up : Direction::Down;
}

bool AnimationClip::isValid() const noexcept {
    if (frames.empty()) {
        return false;
    }
    for (const AnimationFrame& frame : frames) {
        if (!std::isfinite(frame.duration) || frame.duration <= 0.0f) {
            return false;
        }
    }
    return true;
}

float AnimationClip::totalDuration() const noexcept {
    float total = 0.0f;
    for (const AnimationFrame& frame : frames) {
        total += frame.duration;
    }
    return total;
}

}  // namespace fa::anim
