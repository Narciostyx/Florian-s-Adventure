#pragma once

#include <string_view>

namespace fa::core {

using FatalHandler = void (*)(std::string_view message);

/// Installs the process-abort hook. The platform layer uses this to show a
/// message box before dying; tests use it to convert a fatal into a failure.
/// Passing nullptr restores the default (message on stderr).
void setFatalHandler(FatalHandler handler) noexcept;

/// Logs at LogLevel::Fatal, flushes the sinks, invokes the fatal handler, aborts.
[[noreturn]] void fatal(std::string_view message);

/// Logs a recoverable programming error at LogLevel::Error and always returns
/// false, so it can be used as `if (!FA_CHECK(...)) { ... }`.
bool reportCheckFailure(std::string_view expression, std::string_view file, int line, std::string_view message);

}  // namespace fa::core

/// Invariants: compiled out when NDEBUG is set, and abort loudly otherwise.
/// These must never guard user input or content data - that belongs in Result.
#ifndef NDEBUG
#define FA_ASSERT(condition, message)                                                                                  \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            ::fa::core::fatal(message);                                                                                \
        }                                                                                                              \
    } while (false)
#else
#define FA_ASSERT(condition, message) ((void)0)
#endif

/// Same as FA_ASSERT but kept in every configuration. Use only for invariants
/// whose violation would be silently corrupting rather than merely a crash.
#define FA_VERIFY(condition, message)                                                                                  \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            ::fa::core::fatal(message);                                                                                \
        }                                                                                                              \
    } while (false)

/// Non-fatal check for cases we can recover from: logs and returns false.
#define FA_CHECK(condition, message)                                                                                   \
    ((condition) ? true : ::fa::core::reportCheckFailure(#condition, __FILE__, __LINE__, message))
