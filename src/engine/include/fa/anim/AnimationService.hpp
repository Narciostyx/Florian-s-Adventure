#pragma once

#include <memory>

#include <fa/anim/Animator.hpp>

namespace fa::anim {

/// \brief Stateless clip playback, exposed as an interface for tests and tools.
class IAnimationService {
public:
    virtual ~IAnimationService() = default;

    /// \brief Starts \p clip. Without \p restart, a new clip restarts but the same
    /// clip continues from its current time.
    virtual void play(Animator& animator, std::shared_ptr<const AnimationClip> clip, bool restart = false) = 0;
    virtual void stop(Animator& animator) = 0;
    virtual void update(Animator& animator, float deltaSeconds) = 0;
    [[nodiscard]] virtual const AnimationFrame* currentFrame(const Animator& animator) const = 0;
};

/// \brief Default playback implementation.
///
/// Time advances in the caller's fixed step; a large delta may cross several frames,
/// and every entered frame contributes its event at most once per crossing.
class AnimationService final : public IAnimationService {
public:
    void play(Animator& animator, std::shared_ptr<const AnimationClip> clip, bool restart = false) override;
    void stop(Animator& animator) override;
    void update(Animator& animator, float deltaSeconds) override;
    [[nodiscard]] const AnimationFrame* currentFrame(const Animator& animator) const override;
};

}  // namespace fa::anim
