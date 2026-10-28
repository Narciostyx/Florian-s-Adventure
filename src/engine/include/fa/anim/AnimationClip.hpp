#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <fa/core/Math.hpp>
#include <fa/render/RenderTypes.hpp>

namespace fa::anim {

/// \brief Four-way sprite direction. Diagonal movement picks the dominant axis.
enum class Direction : std::uint8_t {
    Down = 0,
    Up,
    Left,
    Right,
};

/// \brief Picks the dominant axis, falling back to \p fallback for a zero vector.
[[nodiscard]] Direction directionFromVector(core::Vec2 move, Direction fallback = Direction::Down) noexcept;

/// \brief One frame: source rectangle, duration and an optional event name.
struct AnimationFrame {
    core::Rect source{};
    float duration = 0.1f;
    /// \brief Empty means "no event". Events fire when the frame is entered.
    std::string event;
};

/// \brief A texture plus an ordered frame list.
///
/// v1 stores a borrowed `render::TextureHandle` instead of `AssetHandle<Texture>`:
/// the animation module should not pull in the asset pipeline and its `/EHsc`
/// dependency. The loader keeps the texture alive, exactly like `scene::Sprite`.
struct AnimationClip {
    render::TextureHandle texture;
    std::vector<AnimationFrame> frames;
    bool loop = true;

    /// \brief Non-empty, all durations positive and finite.
    [[nodiscard]] bool isValid() const noexcept;

    /// \brief Sum of every frame duration, or 0 for an invalid clip.
    [[nodiscard]] float totalDuration() const noexcept;
};

}  // namespace fa::anim
