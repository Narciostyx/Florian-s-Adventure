#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <fa/anim/AnimationClip.hpp>

namespace fa::anim {

/// \brief One event emitted while advancing an animator.
struct AnimationEvent {
    std::string name;
    std::size_t frame = 0;
};

/// \brief Playback state for one clip. Plain data so it can live in an ECS component.
///
/// `events` is cleared at the start of every `AnimationService::update()`; callers
/// consume it after the update. `eventPending` makes the first frame's event fire on
/// the first update even though `play()` itself does not allocate output.
struct Animator {
    std::shared_ptr<const AnimationClip> clip;
    float time = 0.0f;
    std::size_t frameIndex = 0;
    float speed = 1.0f;
    bool playing = false;
    bool finished = false;
    bool eventPending = true;
    std::vector<AnimationEvent> events;
};

}  // namespace fa::anim
