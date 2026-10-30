#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <fa/core/Result.hpp>

namespace fa::platform {

/// \brief File access using UTF-8 paths.
///
/// Paths are UTF-8 encoded `std::string`. Conversion to the native wide-character
/// form happens inside the implementation, so callers never depend on the
/// process ANSI code page. Every failure is reported as a fa::core::Error in the
/// 2000-2999 range.
class IFileSystem {
public:
    virtual ~IFileSystem() = default;

    /// \brief Whether \p path names an existing file or directory.
    virtual bool exists(std::string_view path) const = 0;

    /// \brief Whether \p path names an existing directory.
    virtual bool isDirectory(std::string_view path) const = 0;

    /// \brief Absolute UTF-8 path of the process working directory; empty when unsupported.
    ///
    /// Not pure on purpose: in-memory test file systems have no notion of a current
    /// directory and inherit the "unsupported" defaults below.
    virtual std::string currentPath() const { return {}; }

    /// \brief Switches the process working directory to \p path; false when unsupported.
    virtual bool setCurrentPath(std::string_view path) {
        static_cast<void>(path);
        return false;
    }

    /// \brief Reads \p path as UTF-8 text; the bytes are returned unmodified.
    virtual fa::core::Result<std::string> readText(std::string_view path) = 0;

    /// \brief Reads \p path as raw bytes without any text translation.
    virtual fa::core::Result<std::string> readBinary(std::string_view path) = 0;

    /// \brief Writes \p content to \p path, truncating any previous contents.
    virtual fa::core::Status writeText(std::string_view path, std::string_view content) = 0;

    /// \brief Writes \p content through a sibling temporary file, then renames it over \p path.
    ///
    /// A reader therefore observes either the previous file or the new one, never
    /// a partially written file. Intended for save files.
    virtual fa::core::Status writeTextAtomic(std::string_view path, std::string_view content) = 0;

    /// \brief Creates \p path and every missing parent directory; succeeds if it already exists.
    virtual fa::core::Status createDirectories(std::string_view path) = 0;

    /// \brief Returns the sorted leaf names of the entries directly inside \p path.
    ///
    /// Full paths are not returned, and sub-directory contents are not walked.
    virtual fa::core::Result<std::vector<std::string>> listDirectory(std::string_view path) = 0;

    /// \brief Removes a file or an empty directory; a missing target is not an error.
    virtual fa::core::Status remove(std::string_view path) = 0;
};

}  // namespace fa::platform
