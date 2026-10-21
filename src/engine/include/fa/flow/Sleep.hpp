#pragma once

namespace fa::flow {

/// \file
/// Sleep recovery rule (M25).
///
/// The player may sleep at any time. A sleep always ends at `kWakeHour` (08:00),
/// so the slept duration depends on when the player went to bed: going to bed at
/// or before midnight gives a full night, and later bedtimes restore
/// proportionally less stamina.

/// \brief Hour at which a new day starts and sleeping ends.
inline constexpr float kWakeHour = 8.0f;
/// \brief Hours of sleep that restore stamina completely.
inline constexpr float kFullSleepHours = 8.0f;
/// \brief Lowest efficiency a very short sleep can have.
inline constexpr float kMinSleepRecovery = 0.01f;

/// \brief Hours from \p timeOfDay to the next wake hour.
///
/// Wraps across midnight. Sleeping exactly at the wake hour counts as a full
/// day (24 h), so the result is always in `(0, 24]`.
[[nodiscard]] inline float sleepHoursUntil(float timeOfDay, float wakeHour = kWakeHour) noexcept {
    float hours = wakeHour - timeOfDay;
    while (hours <= 0.0f) {
        hours += 24.0f;
    }
    while (hours > 24.0f) {
        hours -= 24.0f;
    }
    return hours;
}

/// \brief Fraction of missing stamina restored after \p hours of sleep.
///
/// 1.0 at or above \p fullHours. Below that it falls off quadratically
/// (`(hours / full)^2`) and never drops under `kMinSleepRecovery`, so a
/// one-hour nap still restores the minimum 1%.
[[nodiscard]] inline float sleepRecovery(float hours, float fullHours = kFullSleepHours) noexcept {
    if (!(fullHours > 0.0f) || hours >= fullHours) {
        return 1.0f;
    }
    if (!(hours > 0.0f)) {
        return kMinSleepRecovery;
    }
    const float fraction = hours / fullHours;
    const float value = fraction * fraction;
    return value < kMinSleepRecovery ? kMinSleepRecovery : value;
}

}  // namespace fa::flow
