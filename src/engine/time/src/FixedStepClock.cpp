#include "fa/time/FixedStepClock.hpp"

#include <cmath>

#include <fa/core/Assert.hpp>

namespace fa::time {
namespace {

double sanitizeStep(double fixedStepSeconds) noexcept {
    if (!std::isfinite(fixedStepSeconds) || fixedStepSeconds <= 0.0) {
        FA_ASSERT(false, "FixedStepClock: fixed step must be finite and positive");
        return kFixedStepSeconds;
    }
    return fixedStepSeconds;
}

int sanitizeMaxSteps(int maxStepsPerFrame) noexcept {
    if (maxStepsPerFrame <= 0) {
        FA_ASSERT(false, "FixedStepClock: max steps per frame must be positive");
        return kMaxCatchUpSteps;
    }
    return maxStepsPerFrame;
}

double sanitizeMaxDelta(double maxRealDeltaSeconds) noexcept {
    if (!std::isfinite(maxRealDeltaSeconds) || maxRealDeltaSeconds <= 0.0) {
        FA_ASSERT(false, "FixedStepClock: max real delta must be finite and positive");
        return kMaxRealDeltaSeconds;
    }
    return maxRealDeltaSeconds;
}

}  // namespace

FixedStepClock::FixedStepClock(double fixedStepSeconds, int maxStepsPerFrame, double maxRealDeltaSeconds) noexcept
    : m_fixedStepSeconds(sanitizeStep(fixedStepSeconds)), m_maxRealDeltaSeconds(sanitizeMaxDelta(maxRealDeltaSeconds)),
      m_maxStepsPerFrame(sanitizeMaxSteps(maxStepsPerFrame)) {}

void FixedStepClock::reset() noexcept {
    m_pendingSeconds = 0.0;
    m_lastDeltaSeconds = 0.0;
    m_fixedStepIndex = 0;
    m_droppedSteps = 0;
}

int FixedStepClock::advance(double realDeltaSeconds) noexcept {
    m_lastDeltaSeconds = 0.0;

    if (!std::isfinite(realDeltaSeconds) || realDeltaSeconds <= 0.0) {
        return 0;
    }

    double delta = realDeltaSeconds > m_maxRealDeltaSeconds ? m_maxRealDeltaSeconds : realDeltaSeconds;
    m_lastDeltaSeconds = delta;
    m_pendingSeconds += delta;

    int steps = 0;
    while (m_pendingSeconds >= m_fixedStepSeconds && steps < m_maxStepsPerFrame) {
        m_pendingSeconds -= m_fixedStepSeconds;
        ++m_fixedStepIndex;
        ++steps;
    }

    if (m_pendingSeconds >= m_fixedStepSeconds) {
        const double surplusSteps = std::floor(m_pendingSeconds / m_fixedStepSeconds);
        m_droppedSteps += static_cast<std::uint64_t>(surplusSteps);
        m_pendingSeconds -= surplusSteps * m_fixedStepSeconds;
    }

    if (m_pendingSeconds < 0.0) {
        m_pendingSeconds = 0.0;
    }

    return steps;
}

double FixedStepClock::fixedStepSeconds() const noexcept {
    return m_fixedStepSeconds;
}

int FixedStepClock::maxStepsPerFrame() const noexcept {
    return m_maxStepsPerFrame;
}

double FixedStepClock::maxRealDeltaSeconds() const noexcept {
    return m_maxRealDeltaSeconds;
}

double FixedStepClock::lastDeltaSeconds() const noexcept {
    return m_lastDeltaSeconds;
}

double FixedStepClock::pendingSeconds() const noexcept {
    return m_pendingSeconds;
}

float FixedStepClock::interpolationAlpha() const noexcept {
    const double ratio = m_pendingSeconds / m_fixedStepSeconds;
    if (!(ratio > 0.0)) {
        return 0.0f;
    }
    return ratio >= 1.0 ? 1.0f : static_cast<float>(ratio);
}

std::uint64_t FixedStepClock::fixedStepIndex() const noexcept {
    return m_fixedStepIndex;
}

double FixedStepClock::simulatedSeconds() const noexcept {
    return static_cast<double>(m_fixedStepIndex) * m_fixedStepSeconds;
}

std::uint64_t FixedStepClock::droppedSteps() const noexcept {
    return m_droppedSteps;
}

}  // namespace fa::time
