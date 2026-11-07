#pragma once

#include <atomic>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <fa/core/Log.hpp>

namespace fa::debug {

/// \brief One captured log line.
struct DebugLogLine {
    core::LogLevel level = core::LogLevel::Info;
    core::LogCategory category = core::LogCategory::Core;
    std::string message;
};

/// \brief Formats one line for the overlay or the console.
[[nodiscard]] std::string formatLogLine(const DebugLogLine& line);

/// \brief In-memory `ILogSink` with a bounded ring.
///
/// Logging may happen on worker threads, so the ring is guarded by a spin lock
/// instead of `std::mutex`; the critical section is only a vector append.
class DebugLogSink final : public core::ILogSink {
public:
    /// \brief Creates a ring holding at most \p capacity lines.
    explicit DebugLogSink(std::size_t capacity = 128);

    /// \brief Appends one record when \p level reaches the minimum; oldest lines are
    /// dropped and counted when the ring is full.
    void write(core::LogLevel level, core::LogCategory category, std::string_view message) override;
    void flush() override {}

    /// \brief Thread-safe copy of the current ring.
    [[nodiscard]] std::vector<DebugLogLine> snapshot() const;
    /// \brief Empties the ring; the dropped counter resets too.
    void clear();
    /// \brief Records below this level are ignored.
    void setMinimumLevel(core::LogLevel level) noexcept;
    /// \brief Current minimum level.
    [[nodiscard]] core::LogLevel minimumLevel() const noexcept;
    /// \brief Lines discarded because the ring was full.
    [[nodiscard]] std::size_t dropped() const noexcept;
    [[nodiscard]] std::size_t capacity() const noexcept { return m_capacity; }

private:
    /// \brief Spin-lock pair guarding the ring (logging runs on any thread).
    /// @{
    void lock() const noexcept;
    void unlock() const noexcept;
    /// @}

    mutable std::atomic_flag m_lock{};
    std::vector<DebugLogLine> m_lines;
    std::size_t m_capacity = 128;
    std::size_t m_dropped = 0;
    core::LogLevel m_minimum = core::LogLevel::Info;
};

}  // namespace fa::debug
