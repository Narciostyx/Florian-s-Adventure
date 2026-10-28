#pragma once

#include <cstdint>

namespace fa::core {

/// Seconds and Milliseconds are distinct types on purpose: the frame loop, combat
/// timers and save intervals all deal in time, and mixing units silently is the
/// classic source of "why is this 1000x too fast".
struct Milliseconds;

struct Seconds {
    double value = 0.0;

    constexpr Milliseconds toMilliseconds() const noexcept;
};

struct Milliseconds {
    double value = 0.0;

    constexpr Seconds toSeconds() const noexcept;
};

constexpr Milliseconds Seconds::toMilliseconds() const noexcept {
    return Milliseconds{value * 1000.0};
}
constexpr Seconds Milliseconds::toSeconds() const noexcept {
    return Seconds{value / 1000.0};
}

constexpr Seconds seconds(double value) noexcept {
    return Seconds{value};
}
constexpr Milliseconds milliseconds(double value) noexcept {
    return Milliseconds{value};
}

/// One-shot countdown. `tick` returns true exactly once, on the update where the
/// timer reaches zero, so callers do not need "already fired" flags.
class Countdown {
public:
    constexpr Countdown() noexcept = default;
    explicit constexpr Countdown(Seconds duration) noexcept : m_remaining(duration), m_running(true) {}

    constexpr void start(Seconds duration) noexcept {
        m_remaining = duration;
        m_running = true;
    }

    constexpr void stop() noexcept { m_running = false; }

    constexpr bool isRunning() const noexcept { return m_running; }
    constexpr Seconds remaining() const noexcept { return m_remaining; }

    /// 1 at start, 0 when expired. Returns 1 for a zero-length countdown so
    /// progress bars do not divide by zero.
    constexpr double progress(double totalDuration) const noexcept {
        if (totalDuration <= 0.0) {
            return 1.0;
        }
        const double ratio = 1.0 - (m_remaining.value / totalDuration);
        return ratio < 0.0 ? 0.0 : (ratio > 1.0 ? 1.0 : ratio);
    }

    inline bool tick(Seconds deltaTime) noexcept {
        if (!m_running) {
            return false;
        }
        m_remaining.value -= deltaTime.value;
        if (m_remaining.value > 0.0) {
            return false;
        }
        m_running = false;
        m_remaining.value = 0.0;
        return true;
    }

private:
    Seconds m_remaining{};
    bool m_running = false;
};

}  // namespace fa::core
