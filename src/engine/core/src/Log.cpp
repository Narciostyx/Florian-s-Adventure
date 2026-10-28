#include <fa/core/Log.hpp>

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace fa::core {
namespace {

constexpr std::size_t kMaxSinks = 8;
constexpr std::size_t kFormatBufferSize = 1024;

std::mutex g_logMutex;
ILogSink* g_sinks[kMaxSinks] = {};
std::size_t g_sinkCount = 0;
LogLevel g_minimumLevel = LogLevel::Info;

/// \brief Always-on fallback so a log call before the platform layer registers its
/// sinks is still visible instead of vanishing.
class StderrSink final : public ILogSink {
public:
    void write(LogLevel level, LogCategory category, std::string_view message) override {
        std::fprintf(stderr,
                     "[%-5.*s][%-8.*s] %.*s\n",
                     static_cast<int>(toString(level).size()),
                     toString(level).data(),
                     static_cast<int>(toString(category).size()),
                     toString(category).data(),
                     static_cast<int>(message.size()),
                     message.data());
    }

    void flush() override { std::fflush(stderr); }
};

StderrSink g_stderrSink;

bool isSameSink(ILogSink* a, ILogSink* b) noexcept {
    return a == b;
}

/// \brief Registers the fallback sink exactly once, on the first log call.
///
/// \note A static initialiser would run before other translation units' statics
/// and could be observed as "no sinks" by an early log.
void ensureStderrSink() {
    static const bool registered = []() {
        addLogSink(&g_stderrSink);
        return true;
    }();
    (void)registered;
}

}  // namespace

std::string_view toString(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Trace:
            return "trace";
        case LogLevel::Debug:
            return "debug";
        case LogLevel::Info:
            return "info";
        case LogLevel::Warn:
            return "warn";
        case LogLevel::Error:
            return "error";
        case LogLevel::Fatal:
            return "fatal";
        case LogLevel::Off:
            return "off";
    }
    return "?";
}

std::string_view toString(LogCategory category) noexcept {
    switch (category) {
        case LogCategory::Core:
            return "core";
        case LogCategory::Platform:
            return "platform";
        case LogCategory::Time:
            return "time";
        case LogCategory::Asset:
            return "asset";
        case LogCategory::Render:
            return "render";
        case LogCategory::Input:
            return "input";
        case LogCategory::Audio:
            return "audio";
        case LogCategory::Ui:
            return "ui";
        case LogCategory::Animation:
            return "anim";
        case LogCategory::Ecs:
            return "ecs";
        case LogCategory::Tilemap:
            return "tilemap";
        case LogCategory::Physics:
            return "physics";
        case LogCategory::Save:
            return "save";
        case LogCategory::Content:
            return "content";
        case LogCategory::Gameplay:
            return "game";
        case LogCategory::Ai:
            return "ai";
        case LogCategory::Economy:
            return "economy";
        case LogCategory::Editor:
            return "editor";
        case LogCategory::Events:
            return "events";
        case LogCategory::Count:
            break;
    }
    return "?";
}

void addLogSink(ILogSink* sink) {
    if (sink == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_logMutex);
    for (std::size_t i = 0; i < g_sinkCount; ++i) {
        if (isSameSink(g_sinks[i], sink)) {
            return;
        }
    }
    if (g_sinkCount < kMaxSinks) {
        g_sinks[g_sinkCount++] = sink;
    }
}

void removeLogSink(ILogSink* sink) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    for (std::size_t i = 0; i < g_sinkCount; ++i) {
        if (isSameSink(g_sinks[i], sink)) {
            for (std::size_t j = i + 1; j < g_sinkCount; ++j) {
                g_sinks[j - 1] = g_sinks[j];
            }
            --g_sinkCount;
            g_sinks[g_sinkCount] = nullptr;
            return;
        }
    }
}

void flushLogs() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    for (std::size_t i = 0; i < g_sinkCount; ++i) {
        g_sinks[i]->flush();
    }
}

void setLogLevel(LogLevel minimumLevel) noexcept {
    g_minimumLevel = minimumLevel;
}

LogLevel logLevel() noexcept {
    return g_minimumLevel;
}

bool isLogEnabled(LogLevel level) noexcept {
    return level >= g_minimumLevel && g_minimumLevel != LogLevel::Off;
}

void log(LogCategory category, LogLevel level, std::string_view message) {
    ensureStderrSink();
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (level < g_minimumLevel || g_minimumLevel == LogLevel::Off) {
        return;
    }
    for (std::size_t i = 0; i < g_sinkCount; ++i) {
        g_sinks[i]->write(level, category, message);
    }
}

void logf(LogCategory category, LogLevel level, const char* format, ...) {
    if (!isLogEnabled(level)) {
        return;
    }

    char buffer[kFormatBufferSize];
    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (written < 0) {
        return;
    }
    const auto length =
        written < static_cast<int>(sizeof(buffer)) ? static_cast<std::size_t>(written) : sizeof(buffer) - 1U;
    log(category, level, std::string_view(buffer, length));
}

}  // namespace fa::core
