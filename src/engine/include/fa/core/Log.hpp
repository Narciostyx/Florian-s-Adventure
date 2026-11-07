#pragma once

#include <cstdint>
#include <string_view>

namespace fa::core {

/// \brief Ordered by severity: a sink configured with a minimum level drops everything
/// below it. `Off` silences the logger without removing sinks.
enum class LogLevel : std::uint8_t { Trace, Debug, Info, Warn, Error, Fatal, Off };

/// \brief One category per module. Keep this list aligned with docs/architecture/module-map.md
/// so log filtering from the console maps 1:1 onto modules.
enum class LogCategory : std::uint8_t {
    Core,
    Platform,
    Time,
    Asset,
    Render,
    Input,
    Audio,
    Ui,
    Animation,
    Ecs,
    Tilemap,
    Physics,
    Save,
    Content,
    Gameplay,
    Ai,
    Economy,
    Editor,
    Events,
    Count,
};

/// \brief Stable names for a log level; used by sinks, files and the debug overlay.
std::string_view toString(LogLevel level) noexcept;
/// \brief Stable names for a log category.
std::string_view toString(LogCategory category) noexcept;

/// \brief A log destination (console, file, in-game console). Ownership stays with the
/// caller; the logger only stores the pointer. Sinks must be added before any
/// thread that logs starts, and removed before they are destroyed.
class ILogSink {
public:
    ILogSink() = default;
    virtual ~ILogSink() = default;
    ILogSink(const ILogSink&) = delete;
    ILogSink& operator=(const ILogSink&) = delete;

    /// \brief Emits one record. Called from any thread; must not block.
    virtual void write(LogLevel level, LogCategory category, std::string_view message) = 0;
    /// \brief Pushes buffered output to its destination; called by `flushLogs()`.
    virtual void flush() {}
};

/// \brief Non-owning, thread-safe registration. addLogSink ignores duplicates and
/// silently drops registrations past the fixed capacity (8 sinks).
void addLogSink(ILogSink* sink);
/// \brief Unregisters \p sink when present; safe with unknown or null pointers.
void removeLogSink(ILogSink* sink);
/// \brief Flushes every registered sink on the calling thread.
void flushLogs();

/// \brief Sets the global minimum level; records below it are dropped before reaching
/// any sink. Thread-safe.
void setLogLevel(LogLevel minimumLevel) noexcept;
/// \brief Current global minimum level.
LogLevel logLevel() noexcept;
/// \brief True when records at \p level would reach the sinks; cheap call-site guard.
bool isLogEnabled(LogLevel level) noexcept;

/// \brief Thread-safe. `message` must stay valid for the duration of the call.
void log(LogCategory category, LogLevel level, std::string_view message);

/// \brief printf-style variant for call sites that would otherwise build a temporary
/// string. Output is truncated at 1024 bytes; the format call is skipped entirely
/// when the level is filtered out.
void logf(LogCategory category, LogLevel level, const char* format, ...);

}  // namespace fa::core
