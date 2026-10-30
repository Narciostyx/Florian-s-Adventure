#pragma once

#include <string>
#include <string_view>

#include <fa/core/Log.hpp>
#include <fa/platform/FileSystem.hpp>

namespace fa::app {

/// \brief M02 log sink that keeps one text file per run, `data/log/Log_<时间戳>.txt`.
///
/// Registered through `fa::core::addLogSink`, so it receives every engine line at the
/// configured level next to the always-on console sink; nothing else has to know it exists.
/// Lines are buffered and the file is rewritten as a whole: `IFileSystem` has no append API
/// and a run produces tens of lines, not thousands.
///
/// The logger calls sinks while holding its own mutex, so the file work must never log.
/// `StdFileSystem` does not, which is what makes `flush()` from inside safe here.
class RunLogFileSink final : public fa::core::ILogSink {
public:
    /// \brief Creates the directory, writes the run header and registers the sink.
    RunLogFileSink(fa::platform::IFileSystem& files, std::string path, std::string_view header);
    /// \brief Unregisters first, then writes whatever is still buffered.
    ~RunLogFileSink() override;

    RunLogFileSink(const RunLogFileSink&) = delete;
    RunLogFileSink& operator=(const RunLogFileSink&) = delete;

    void write(fa::core::LogLevel level, fa::core::LogCategory category, std::string_view message) override;
    void flush() override;

    /// \brief Path the sink writes to, whether or not the first write succeeded.
    [[nodiscard]] const std::string& path() const noexcept { return m_path; }

    /// \brief False once a write has failed; the sink then stays silent instead of retrying.
    [[nodiscard]] bool ok() const noexcept { return m_ok; }

private:
    static constexpr std::size_t kFlushBytes = 4096;

    void writeOut();

    fa::platform::IFileSystem* m_files = nullptr;
    std::string m_path;
    std::string m_lines;
    bool m_ok = false;
    bool m_dirty = false;
};

/// \brief `directory/Log_yyyyMMdd-HHmmss.txt` for the current local time.
[[nodiscard]] std::string runLogFilePath(std::string_view directory);

}  // namespace fa::app
