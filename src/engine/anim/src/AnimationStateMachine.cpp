#include "fa/anim/AnimationStateMachine.hpp"

#include <utility>

namespace fa::anim {

std::shared_ptr<const AnimationClip> DirectionalClips::forDirection(Direction direction) const {
    switch (direction) {
        case Direction::Down:
            if (down) {
                return down;
            }
            break;
        case Direction::Up:
            if (up) {
                return up;
            }
            break;
        case Direction::Left:
            if (left) {
                return left;
            }
            break;
        case Direction::Right:
            if (right) {
                return right;
            }
            break;
    }
    if (down) {
        return down;
    }
    if (up) {
        return up;
    }
    if (left) {
        return left;
    }
    return right;
}

bool AnimationStateMachine::addState(std::string name, DirectionalClips clips) {
    if (name.empty() || findState(name) != nullptr) {
        return false;
    }
    m_states.push_back(State{std::move(name), std::move(clips)});
    return true;
}

bool AnimationStateMachine::addState(std::string name, std::shared_ptr<const AnimationClip> clip) {
    DirectionalClips clips;
    clips.down = std::move(clip);
    return addState(std::move(name), std::move(clips));
}

bool AnimationStateMachine::addTransition(AnimationTransition transition) {
    if (transition.to.empty() || !transition.condition) {
        return false;
    }
    m_transitions.push_back(std::move(transition));
    return true;
}

bool AnimationStateMachine::setState(std::string_view name) {
    if (findState(name) == nullptr) {
        return false;
    }
    m_state = std::string(name);
    m_forceRestart = true;
    return true;
}

void AnimationStateMachine::update(Animator& animator, const AnimationParams& params, float deltaSeconds) {
    if (m_states.empty()) {
        return;
    }

    if (m_state.empty()) {
        m_state = m_states.front().name;
        m_forceRestart = true;
    }

    const State* current = findState(m_state);
    if (current == nullptr) {
        m_state = m_states.front().name;
        current = &m_states.front();
        m_forceRestart = true;
    }

    const AnimationTransition* chosen = nullptr;
    for (const AnimationTransition& transition : m_transitions) {
        if (!transition.from.empty() && transition.from != m_state) {
            continue;
        }
        if (!transition.condition(params)) {
            continue;
        }
        if (chosen == nullptr || transition.priority > chosen->priority) {
            chosen = &transition;
        }
    }

    bool restart = m_forceRestart;
    if (chosen != nullptr && chosen->to != m_state) {
        if (const State* target = findState(chosen->to); target != nullptr) {
            m_state = target->name;
            current = target;
            restart = true;
        }
    }
    m_forceRestart = false;

    if (current != nullptr) {
        std::shared_ptr<const AnimationClip> clip = current->clips.forDirection(params.direction);
        if (!clip) {
            m_service.stop(animator);
        } else if (restart || animator.clip != clip) {
            m_service.play(animator, std::move(clip), true);
        }
    }

    m_service.update(animator, deltaSeconds);
}

const AnimationStateMachine::State* AnimationStateMachine::findState(std::string_view name) const noexcept {
    for (const State& state : m_states) {
        if (state.name == name) {
            return &state;
        }
    }
    return nullptr;
}

}  // namespace fa::anim
