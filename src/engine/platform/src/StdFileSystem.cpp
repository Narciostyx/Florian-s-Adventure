#include "StdFileSystem.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>

#include <fa/platform/PlatformError.hpp>

namespace fa::platform::detail {
namespace {

std::filesystem::path toPath(std::string_view utf8) {
    std::u8string text;
    text.reserve(utf8.size());
    for (const char byte : utf8) {
        text.push_back(static_cast<char8_t>(static_cast<unsigned char>(byte)));
    }
    return std::filesystem::path(text);
}

std::string fromPath(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    std::string utf8;
    utf8.reserve(text.size());
    for (const char8_t unit : text) {
        utf8.push_back(static_cast<char>(unit));
    }
    return utf8;
}

std::filesystem::path temporaryPath(const std::filesystem::path& target) {
    std::filesystem::path temporary = target;
    temporary += ".tmp";
    return temporary;
}

}  // namespace

bool StdFileSystem::exists(std::string_view path) const {
    std::error_code ec;
    const bool found = std::filesystem::exists(toPath(path), ec);
    return !ec && found;
}

bool StdFileSystem::isDirectory(std::string_view path) const {
    std::error_code ec;
    const bool directory = std::filesystem::is_directory(toPath(path), ec);
    return !ec && directory;
}

fa::core::Result<std::string> StdFileSystem::readBinary(std::string_view path) {
    std::ifstream stream(toPath(path), std::ios::binary);
    if (!stream) {
        return fa::core::Result<std::string>::err(platform_error::kFileOpenFailed,
                                                  "cannot open file for reading: " + std::string(path));
    }

    std::string bytes;
    stream.seekg(0, std::ios::end);
    const std::streamoff size = stream.tellg();
    if (size > 0) {
        stream.seekg(0, std::ios::beg);
        bytes.resize(static_cast<std::size_t>(size));
        stream.read(bytes.data(), static_cast<std::streamsize>(size));
        if (!stream) {
            return fa::core::Result<std::string>::err(platform_error::kFileReadFailed,
                                                      "cannot read file: " + std::string(path));
        }
    }
    return fa::core::Result<std::string>::ok(std::move(bytes));
}

fa::core::Result<std::string> StdFileSystem::readText(std::string_view path) {
    return readBinary(path);
}

fa::core::Status StdFileSystem::writeText(std::string_view path, std::string_view content) {
    std::ofstream stream(toPath(path), std::ios::binary | std::ios::trunc);
    if (!stream) {
        return fa::core::errorStatus(platform_error::kFileOpenFailed,
                                     "cannot open file for writing: " + std::string(path));
    }

    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!stream) {
        return fa::core::errorStatus(platform_error::kFileWriteFailed, "cannot write file: " + std::string(path));
    }
    return fa::core::okStatus();
}

fa::core::Status StdFileSystem::writeTextAtomic(std::string_view path, std::string_view content) {
    const std::filesystem::path target = toPath(path);
    const std::filesystem::path temporary = temporaryPath(target);

    const fa::core::Status staging = writeText(fromPath(temporary), content);
    if (staging.isError()) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return staging;
    }

    std::error_code ec;
    std::filesystem::rename(temporary, target, ec);
    if (!ec) {
        return fa::core::okStatus();
    }

    std::filesystem::remove(target, ec);
    ec.clear();
    std::filesystem::rename(temporary, target, ec);
    if (!ec) {
        return fa::core::okStatus();
    }

    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return fa::core::errorStatus(platform_error::kFileWriteFailed,
                                 "cannot replace file atomically: " + std::string(path));
}

fa::core::Status StdFileSystem::createDirectories(std::string_view path) {
    std::error_code ec;
    std::filesystem::create_directories(toPath(path), ec);
    if (ec) {
        return fa::core::errorStatus(platform_error::kDirectoryCreationFailed,
                                     "cannot create directory: " + std::string(path));
    }
    return fa::core::okStatus();
}

fa::core::Result<std::vector<std::string>> StdFileSystem::listDirectory(std::string_view path) {
    std::error_code ec;
    std::filesystem::directory_iterator iterator(toPath(path), ec);
    if (ec) {
        return fa::core::Result<std::vector<std::string>>::err(platform_error::kDirectoryEnumerationFailed,
                                                               "cannot open directory: " + std::string(path));
    }

    std::vector<std::string> names;
    const std::filesystem::directory_iterator end;
    while (iterator != end) {
        names.push_back(fromPath(iterator->path().filename()));
        iterator.increment(ec);
        if (ec) {
            return fa::core::Result<std::vector<std::string>>::err(platform_error::kDirectoryEnumerationFailed,
                                                                   "cannot enumerate directory: " + std::string(path));
        }
    }

    std::sort(names.begin(), names.end());
    return fa::core::Result<std::vector<std::string>>::ok(std::move(names));
}

fa::core::Status StdFileSystem::remove(std::string_view path) {
    std::error_code ec;
    std::filesystem::remove(toPath(path), ec);
    if (ec) {
        return fa::core::errorStatus(platform_error::kFileWriteFailed, "cannot remove: " + std::string(path));
    }
    return fa::core::okStatus();
}

}  // namespace fa::platform::detail
