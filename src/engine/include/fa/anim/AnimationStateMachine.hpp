#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <fa/anim/AnimationService.hpp>

namespace fa::anim {

/// \brief Gameplay-facing parameters the state machine reads.
struct AnimationParams {
    core::Vec2 move{};
    float speed = 0.0f;
    Direction direction = Direction::Down;
    bool running = false;
    bool attacking = false;
    bool dodging = false;
    bool hurt = false;
};

/// \brief One clip per direction. Missing directions fall back to `down`.
struct DirectionalClips {
    std::shared_ptr<const AnimationClip> down;
    std::shared_ptr<const AnimationClip> up;
    std::shared_ptr<const AnimationClip> left;
    std::shared_ptr<const AnimationClip> right;

    [[nodiscard]] std::shared_ptr<const AnimationClip> forDirection(Direction direction) const;
};

/// \brief A named state transition; empty `from` means "from any state".
struct AnimationTransition {
    std::string from;
    std::string to;
    std::function<bool(const AnimationParams&)> condition;
    /// Higher wins when several transitions match; ties keep registration order.
    int priority = 0;
};

/// \brief Thin state layer over `AnimationService`.
///
/// States own direction variants; a transition switches the state and restarts the
/// clip. Direction changes within one state also switch clips and restart. Gameplay
/// rules (when attacking ends, etc.) stay in M16/M20.
class AnimationStateMachine {
public:
    [[nodiscard]] bool addState(std::string name, DirectionalClips clips);
    [[nodiscard]] bool addState(std::string name, std::shared_ptr<const AnimationClip> clip);
    [[nodiscard]] bool addTransition(AnimationTransition transition);

    /// \brief Selects a state by name. The next `update()` restarts regardless of
    /// whether the clip pointer happens to match.
    [[nodiscard]] bool setState(std::string_view name);
    [[nodiscard]] std::string_view state() const noexcept { return m_state; }

    void update(Animator& animator, const AnimationParams& params, float deltaSeconds);

    [[nodiscard]] AnimationService& service() noexcept { return m_service; }
    [[nodiscard]] const AnimationService& service() const noexcept { return m_service; }

private:
    struct State {
        std::string name;
        DirectionalClips clips;
    };

    [[nodiscard]] const State* findState(std::string_view name) const noexcept;

    AnimationService m_service;
    std::vector<State> m_states;
    std::vector<AnimationTransition> m_transitions;
    std::string m_state;
    bool m_forceRestart = false;
};

}  // namespace fa::anim
