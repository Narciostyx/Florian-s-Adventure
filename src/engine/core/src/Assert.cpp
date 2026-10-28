#include <fa/core/Assert.hpp>

#include <cstdio>
#include <cstdlib>

#include <fa/core/Log.hpp>

namespace fa::core {
namespace {

void defaultFatalHandler(std::string_view message) noexcept {
    std::fputs("\n[fatal] ", stderr);
    std::fwrite(message.data(), 1, message.size(), stderr);
    std::fputc('\n', stderr);
    std::fflush(stderr);
}

FatalHandler g_fatalHandler = &defaultFatalHandler;

}  // namespace

void setFatalHandler(FatalHandler handler) noexcept {
    g_fatalHandler = handler != nullptr ? handler : &defaultFatalHandler;
}

[[noreturn]] void fatal(std::string_view message) {
    /// \note Log first: a custom handler may show a dialog, and we still want the
    /// message in the log file for the post-mortem.
    log(LogCategory::Core, LogLevel::Fatal, message);
    flushLogs();
    g_fatalHandler(message);
    std::abort();
}

bool reportCheckFailure(std::string_view expression, std::string_view file, int line, std::string_view message) {
    char buffer[512];
    const int written = std::snprintf(buffer,
                                      sizeof(buffer),
                                      "%.*s:%d: check failed: %.*s",
                                      static_cast<int>(file.size()),
                                      file.data(),
                                      line,
                                      static_cast<int>(expression.size()),
                                      expression.data());
    if (written > 0) {
        const auto length =
            written < static_cast<int>(sizeof(buffer)) ? static_cast<std::size_t>(written) : sizeof(buffer) - 1U;
        log(LogCategory::Core, LogLevel::Error, std::string_view(buffer, length));
    }
    if (!message.empty()) {
        log(LogCategory::Core, LogLevel::Error, message);
    }
    return false;
}

}  // namespace fa::core
