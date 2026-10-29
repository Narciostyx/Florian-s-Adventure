#pragma once

#include <fa/platform/FileSystem.hpp>

namespace fa::platform::detail {

/// \brief IFileSystem backed by std::filesystem and binary iostreams.
///
/// All operations translate UTF-8 paths to std::filesystem::path through
/// char8_t, so the process locale never influences path handling.
class StdFileSystem final : public IFileSystem {
public:
    StdFileSystem() = default;
    ~StdFileSystem() override = default;

    StdFileSystem(const StdFileSystem&) = delete;
    StdFileSystem& operator=(const StdFileSystem&) = delete;

    bool exists(std::string_view path) const override;
    bool isDirectory(std::string_view path) const override;
    std::string currentPath() const override;
    bool setCurrentPath(std::string_view path) override;
    fa::core::Result<std::string> readText(std::string_view path) override;
    fa::core::Result<std::string> readBinary(std::string_view path) override;
    fa::core::Status writeText(std::string_view path, std::string_view content) override;
    fa::core::Status writeTextAtomic(std::string_view path, std::string_view content) override;
    fa::core::Status createDirectories(std::string_view path) override;
    fa::core::Result<std::vector<std::string>> listDirectory(std::string_view path) override;
    fa::core::Status remove(std::string_view path) override;
};

}  // namespace fa::platform::detail
