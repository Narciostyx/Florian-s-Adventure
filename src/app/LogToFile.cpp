#include "LogToFile.hpp"

#include <ctime>

namespace fa::app {

namespace {

/// \brief Everything up to and excluding the last path separator.
[[nodiscard]] std::string_view parentOf(std::string_view path) {
    const std::size_t separator = path.find_last_of('/');
    return separator == std::string_view::npos ? std::string_view{} : path.substr(0, separator);
}

}  // namespace

RunLogFileSink::RunLogFileSink(fa::platform::IFileSystem& files, std::string path, std::string_view header)
    : m_files(&files), m_path(std::move(path)) {
    const std::string_view directory = parentOf(m_path);
    if (!directory.empty()) {
        static_cast<void>(m_files->createDirectories(directory));
    }
    m_lines = "-- ";
    m_lines += header;
    m_lines += " --\r\n";
    writeOut();
    if (m_ok) {
        fa::core::addLogSink(this);
    }
}

RunLogFileSink::~RunLogFileSink() {
    fa::core::removeLogSink(this);
    writeOut();
}

void RunLogFileSink::write(const fa::core::LogLevel level,
                           const fa::core::LogCategory category,
                           std::string_view message) {
    if (!m_ok) {
        return;
    }
    const std::string_view levelName = fa::core::toString(level);
    const std::string_view categoryName = fa::core::toString(category);
    m_lines.append("[");
    m_lines.append(levelName.data(), levelName.size());
    m_lines.append("][");
    m_lines.append(categoryName.data(), categoryName.size());
    m_lines.append("] ");
    m_lines.append(message.data(), message.size());
    m_lines.append("\r\n");
    m_dirty = true;
    if (m_lines.size() >= kFlushBytes) {
        writeOut();
    }
}

void RunLogFileSink::flush() {
    if (m_dirty) {
        writeOut();
    }
}

void RunLogFileSink::writeOut() {
    if (m_files == nullptr) {
        return;
    }
    m_ok = m_files->writeText(m_path, m_lines).isOk();
    m_dirty = false;
}

std::string runLogFilePath(std::string_view directory) {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    static_cast<void>(localtime_s(&local, &now));
#else
    const std::tm* pointer = std::localtime(&now);
    if (pointer != nullptr) {
        local = *pointer;
    }
#endif
    char stamp[32]{};
    static_cast<void>(std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local));
    return std::string(directory) + "/Log_" + stamp + ".txt";
}

}  // namespace fa::app
