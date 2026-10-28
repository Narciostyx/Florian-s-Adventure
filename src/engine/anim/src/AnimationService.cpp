#include "fa/anim/AnimationService.hpp"

#include <cmath>
#include <cstddef>
#include <utility>

namespace fa::anim {
namespace {

void emitCurrentEvent(Animator& animator) {
    if (!animator.clip || animator.frameIndex >= animator.clip->frames.size()) {
        return;
    }
    const AnimationFrame& frame = animator.clip->frames[animator.frameIndex];
    if (!frame.event.empty()) {
        animator.events.push_back(AnimationEvent{frame.event, animator.frameIndex});
    }
}

}  // namespace

void AnimationService::play(Animator& animator, std::shared_ptr<const AnimationClip> clip, bool restart) {
    if (!clip || !clip->isValid()) {
        stop(animator);
        return;
    }

    const bool changed = animator.clip != clip;
    animator.events.clear();
    animator.clip = std::move(clip);
    if (restart || changed) {
        animator.time = 0.0f;
        animator.frameIndex = 0;
        animator.finished = false;
        animator.eventPending = true;
    }
    animator.playing = true;
}

void AnimationService::stop(Animator& animator) {
    animator.playing = false;
    animator.events.clear();
}

void AnimationService::update(Animator& animator, float deltaSeconds) {
    animator.events.clear();
    if (!animator.playing || !animator.clip || !animator.clip->isValid()) {
        return;
    }

    if (animator.eventPending) {
        emitCurrentEvent(animator);
        animator.eventPending = false;
    }
    if (animator.finished) {
        return;
    }
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f || animator.speed <= 0.0f) {
        return;
    }

    const AnimationClip& clip = *animator.clip;
    animator.time += deltaSeconds * animator.speed;

    /// A loop with a tiny frame duration and a large delta still has to terminate.
    const std::size_t maxIterations = clip.frames.size() * 64u + 64u;
    std::size_t iterations = 0;
    while (animator.frameIndex < clip.frames.size() && iterations < maxIterations) {
        const float duration = clip.frames[animator.frameIndex].duration;
        if (animator.time < duration) {
            break;
        }
        animator.time -= duration;
        ++animator.frameIndex;
        ++iterations;

        if (animator.frameIndex >= clip.frames.size()) {
            if (clip.loop) {
                animator.frameIndex = 0;
            } else {
                animator.frameIndex = clip.frames.size() - 1u;
                animator.time = 0.0f;
                animator.finished = true;
                animator.playing = false;
                break;
            }
        }
        emitCurrentEvent(animator);
    }
}

const AnimationFrame* AnimationService::currentFrame(const Animator& animator) const {
    if (!animator.clip || animator.frameIndex >= animator.clip->frames.size()) {
        return nullptr;
    }
    return &animator.clip->frames[animator.frameIndex];
}

}  // namespace fa::anim
