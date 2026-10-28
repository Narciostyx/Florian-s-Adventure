#pragma once

#include <fa/core/Error.hpp>

namespace fa::save::save_error {

/// \brief Slot name is empty, too long, or contains path separators.
inline constexpr int kInvalidSlot = 6100;

/// \brief Reading or writing the slot file failed.
inline constexpr int kIoFailure = 6101;

/// \brief The file is not valid JSON or lacks the save header.
inline constexpr int kParseFailed = 6102;

/// \brief The save was written by a newer engine.
inline constexpr int kUnsupportedVersion = 6103;

/// \brief Both the primary file and its backup are unreadable.
inline constexpr int kCorruptSave = 6104;

/// \brief Rotating the previous file to `.bak` failed.
inline constexpr int kBackupFailed = 6105;

/// \brief The requested slot does not exist.
inline constexpr int kSlotNotFound = 6106;

/// \brief No migration path from the stored version to the current one.
inline constexpr int kMigrationFailed = 6107;

}  // namespace fa::save::save_error
