#pragma once

#include <string>

namespace fa::content::content_error {

/// \file
/// Error codes for M15. Save and content share the 6000 block.

/// \brief The document is not valid JSON.
inline constexpr int kParseFailed = 6000;

/// \brief The document declares a schema version this build does not understand.
inline constexpr int kUnsupportedSchema = 6001;

/// \brief A content id is malformed or uses the wrong namespace prefix.
inline constexpr int kInvalidId = 6002;

/// \brief Two definitions of the same type use the same id.
inline constexpr int kDuplicateId = 6003;

/// \brief A crop, recipe, enemy or loot entry references a missing definition.
inline constexpr int kMissingReference = 6004;

/// \brief A required field is missing or has the wrong type/value.
inline constexpr int kInvalidField = 6005;

/// \brief One or more validation errors were collected; see `errors()`.
inline constexpr int kValidationFailed = 6006;

/// \brief A season name or mask is invalid.
inline constexpr int kInvalidSeason = 6007;

/// \brief Stack sizes, prices, amounts or chances are out of range.
inline constexpr int kInvalidAmount = 6008;

}  // namespace fa::content::content_error

namespace fa::content {

/// \brief One collected content problem.
///
/// Loading never throws: structural errors are collected with their JSON context,
/// then reference errors are appended by `validate()`. The caller can print every
/// error instead of fixing one per run.
struct ContentError {
    int code = 0;
    /// \brief Content id or JSON path that failed.
    std::string context;
    std::string message;
};

}  // namespace fa::content
