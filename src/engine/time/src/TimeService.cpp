#include "fa/time/TimeService.hpp"

#include <cmath>

namespace fa::time {

TimeService::TimeService() noexcept {
    for (DomainState& domain : m_domains) {
        domain.scale = 1.0f;
        domain.paused = false;
    }
}

const TimeService::DomainState& TimeService::state(TimeDomain domain) const noexcept {
    const std::size_t index = static_cast<std::size_t>(domain);
    if (index >= kTimeDomainCount) {
        return m_domains[static_cast<std::size_t>(TimeDomain::Gameplay)];
    }
    return m_domains[index];
}

void TimeService::setPaused(TimeDomain domain, bool paused) {
    const std::size_t index = static_cast<std::size_t>(domain);
    if (index >= kTimeDomainCount) {
        return;
    }
    m_domains[index].paused = paused;
}

bool TimeService::isPaused(TimeDomain domain) const {
    return state(domain).paused;
}

void TimeService::setScale(TimeDomain domain, float scale) {
    const std::size_t index = static_cast<std::size_t>(domain);
    if (index >= kTimeDomainCount) {
        return;
    }
    if (!std::isfinite(scale)) {
        return;
    }
    m_domains[index].scale = scale < 0.0f ? 0.0f : (scale > kMaxTimeScale ? kMaxTimeScale : scale);
}

float TimeService::scale(TimeDomain domain) const {
    return state(domain).scale;
}

float TimeService::effectiveScale(TimeDomain domain) const {
    const DomainState& domainState = state(domain);
    return domainState.paused ? 0.0f : domainState.scale;
}

double TimeService::deltaFor(TimeDomain domain) const {
    return m_context.realDeltaSeconds * static_cast<double>(effectiveScale(domain));
}

const FrameContext& TimeService::context() const {
    return m_context;
}

void TimeService::beginFrame(double realDeltaSeconds,
                             std::uint32_t fixedStepsThisFrame,
                             float interpolationAlpha,
                             std::uint64_t fixedStepIndex,
                             double fixedDeltaSeconds) noexcept {
    ++m_context.frameIndex;
    m_context.realDeltaSeconds = realDeltaSeconds;
    m_context.fixedStepsThisFrame = fixedStepsThisFrame;
    m_context.interpolationAlpha = interpolationAlpha;
    m_context.fixedStepIndex = fixedStepIndex;
    m_context.fixedDeltaSeconds = fixedDeltaSeconds;

    const DomainState& gameplay = state(TimeDomain::Gameplay);
    m_context.gameplayScale = gameplay.paused ? 0.0f : gameplay.scale;
    m_context.isPaused = gameplay.paused;
    m_context.scaledDeltaSeconds = realDeltaSeconds * static_cast<double>(m_context.gameplayScale);
}

void TimeService::endFrame() noexcept {
    m_context.fixedStepsThisFrame = 0;
    m_context.interpolationAlpha = 0.0f;
}

}  // namespace fa::time
