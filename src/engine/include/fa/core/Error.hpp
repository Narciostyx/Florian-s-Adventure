#pragma once

#include <string>
#include <string_view>
#include <utility>

namespace fa::core {

/// Error codes are plain integers, not an enum class, so gameplay modules can add
/// their own ranges without editing this file (see docs/modules/01-core-foundation.md
/// section 6). Reserved ranges:
///     1000-1999  Core
///     2000-2999  Platform
///     3000-3999  Assets
///     4000-4999  Render / Input / Audio / UI
///     5000-5999  Ecs / Physics
///     6000-6999  Save / Content
///     7000-7999  Gameplay
namespace error_code {

inline constexpr int kUnknown = 1000;
inline constexpr int kInvalidArgument = 1001;
inline constexpr int kOutOfRange = 1002;
inline constexpr int kNotFound = 1003;
inline constexpr int kAlreadyExists = 1004;
inline constexpr int kUnsupported = 1005;
inline constexpr int kIoFailure = 1006;
inline constexpr int kParseFailure = 1007;
inline constexpr int kAssertionFailed = 1008;

}  // namespace error_code

/// Recoverable failure description. Exactly one `Error` is carried by a failed
/// Result; successful values never allocate an Error.
struct Error {
    int code = error_code::kUnknown;
    std::string message;

    Error() = default;
    Error(int errorCode, std::string text) : code(errorCode), message(std::move(text)) {}
    Error(int errorCode, std::string_view text) : code(errorCode), message(text) {}
};

inline Error makeError(int code, std::string message) {
    return Error(code, std::move(message));
}

inline Error makeError(int code, std::string_view message) {
    return Error(code, message);
}

}  // namespace fa::core
