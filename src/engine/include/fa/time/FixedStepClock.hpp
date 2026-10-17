#pragma once

#include <cstdint>

#include <fa/time/FrameContext.hpp>

namespace fa::time {

/// \brief Fixed-step accumulator that turns irregular wall-clock deltas into a
/// whole number of simulation steps.
///
/// Pure logic: no clock, no platform, no allocation, so frame pacing can be
/// tested headlessly and deterministically.
///
/// Integer tick counting is authoritative. The leftover fraction is bounded by
/// one step and is consumed every frame, so no error accumulates over a long run.
class FixedStepClock {
public:
    /// \brief Creates a clock.
    ///
    /// Out-of-range arguments are replaced by the defaults and asserted in Debug:
    /// these come from configuration, never from player input.
    explicit FixedStepClock(double fixedStepSeconds = kFixedStepSeconds,
                            int maxStepsPerFrame = kMaxCatchUpSteps,
                            double maxRealDeltaSeconds = kMaxRealDeltaSeconds) noexcept;

    /// \brief Clears the accumulator and the tick counters.
    ///
    /// Pending time is discarded on purpose: after a load screen, the time spent
    /// loading must not be simulated.
    void reset() noexcept;

    /// \brief Accumulates \p realDeltaSeconds and returns how many fixed steps to run.
    ///
    /// Values that are zero, negative or not finite are ignored. The delta is
    /// clamped to maxRealDeltaSeconds(), and the result never exceeds
    /// maxStepsPerFrame(); surplus whole steps are counted in droppedSteps().
    int advance(double realDeltaSeconds) noexcept;

    /// \brief Length of one fixed step in seconds.
    double fixedStepSeconds() const noexcept;

    /// \brief Upper bound returned by a single advance() call.
    int maxStepsPerFrame() const noexcept;

    /// \brief Largest wall-clock delta this clock accepts in one advance() call.
    double maxRealDeltaSeconds() const noexcept;

    /// \brief Wall-clock delta accepted by the last advance(), after clamping.
    double lastDeltaSeconds() const noexcept;

    /// \brief Time accumulated but not yet turned into fixed steps, in `[0, fixedStepSeconds)`.
    double pendingSeconds() const noexcept;

    /// \brief Position between the last two fixed states, in `[0, 1)`.
    float interpolationAlpha() const noexcept;

    /// \brief Number of fixed steps executed since construction or reset().
    std::uint64_t fixedStepIndex() const noexcept;

    /// \brief Simulated seconds, always `fixedStepIndex * fixedStepSeconds`.
    double simulatedSeconds() const noexcept;

    /// \brief Fixed steps discarded because maxStepsPerFrame() was hit.
    ///
    /// A non-zero value means the machine cannot keep up with the configured
    /// simulation rate; it is a tuning signal, not an error.
    std::uint64_t droppedSteps() const noexcept;

private:
    double m_fixedStepSeconds = kFixedStepSeconds;
    double m_maxRealDeltaSeconds = kMaxRealDeltaSeconds;
    double m_pendingSeconds = 0.0;
    double m_lastDeltaSeconds = 0.0;
    std::uint64_t m_fixedStepIndex = 0;
    std::uint64_t m_droppedSteps = 0;
    int m_maxStepsPerFrame = kMaxCatchUpSteps;
};

}  // namespace fa::time
