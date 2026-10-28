#pragma once

#include <cstdint>

namespace fa::time {

/// \brief Simulation rate. Gameplay logic advances in whole steps of this length
/// so behaviour is identical at any frame rate.
inline constexpr int kFixedStepsPerSecond = 60;

/// \brief Length of one fixed step in seconds.
inline constexpr double kFixedStepSeconds = 1.0 / static_cast<double>(kFixedStepsPerSecond);

/// \brief Upper bound on fixed steps executed in a single frame.
///
/// Without it a long stall (breakpoint, window drag, disk hitch) would queue
/// thousands of steps and the next frame would take even longer, which is the
/// classic death spiral.
inline constexpr int kMaxCatchUpSteps = 5;

/// \brief Wall-clock delta is clamped to this before it reaches the accumulator.
///
/// A stall is a stall: replaying several seconds of simulation at once is never
/// the behaviour a player wants.
inline constexpr double kMaxRealDeltaSeconds = 0.25;

/// \brief Largest accepted time scale, to keep a bad settings value from turning
/// the simulation into a slideshow or a fast-forward.
inline constexpr float kMaxTimeScale = 8.0f;

/// \brief Read-only time view handed to every system once per frame.
///
/// Gameplay code never reads a real clock; this snapshot is the only time source.
/// Fields fall into two groups: *real* timing for UI and animation pacing, and
/// *scaled* timing for anything that should respect pause and slow motion.
struct FrameContext {
    /// \brief 0 for the first frame, incremented once per loop iteration.
    std::uint64_t frameIndex = 0;

    /// \brief Authoritative simulated tick count after this frame's fixed steps.
    ///
    /// Integer ticks, not accumulated floats, are the source of truth for
    /// simulation time: `fixedStepIndex * fixedDeltaSeconds` never drifts.
    std::uint64_t fixedStepIndex = 0;

    /// \brief Clamped wall-clock delta since the previous frame.
    double realDeltaSeconds = 0.0;

    /// \brief Gameplay delta after pause and scale, i.e. what fixed steps consumed.
    double scaledDeltaSeconds = 0.0;

    /// \brief Length of one fixed step, repeated here so systems need no constants.
    double fixedDeltaSeconds = kFixedStepSeconds;

    /// \brief Position between the last two fixed states, in `[0, 1)`.
    ///
    /// Render-only: logical code must never branch on it, or rendering and
    /// simulation start disagreeing about what happened.
    float interpolationAlpha = 0.0f;

    /// \brief Effective gameplay scale, already zero while paused.
    float gameplayScale = 1.0f;

    /// \brief Fixed steps executed this frame, in `[0, kMaxCatchUpSteps]`.
    std::uint32_t fixedStepsThisFrame = 0;

    /// \brief Whether the gameplay domain was paused for this frame.
    bool isPaused = false;
};

}  // namespace fa::time
