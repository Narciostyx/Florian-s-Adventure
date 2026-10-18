#pragma once

#include <fa/core/Error.hpp>

namespace fa::platform::platform_error {

/// \brief SDL initialization (or a required subsystem) failed.
inline constexpr int kSdlInitFailed = 2000;

/// \brief Window creation failed.
inline constexpr int kWindowCreationFailed = 2001;

/// \brief A file could not be opened for reading or writing.
inline constexpr int kFileOpenFailed = 2002;

/// \brief A file opened successfully but reading its contents failed.
inline constexpr int kFileReadFailed = 2003;

/// \brief Writing or atomically replacing a file failed.
inline constexpr int kFileWriteFailed = 2004;

/// \brief Creating a directory tree failed.
inline constexpr int kDirectoryCreationFailed = 2005;

/// \brief A directory could not be opened or enumerated.
inline constexpr int kDirectoryEnumerationFailed = 2006;

/// \brief Displaying a modal message box failed.
inline constexpr int kMessageBoxFailed = 2007;

/// \brief The driver refused to create the requested OpenGL context.
inline constexpr int kGlContextCreationFailed = 2008;

/// \brief This backend has no OpenGL support at all.
inline constexpr int kGlUnsupported = 2009;

/// \brief The audio subsystem or the default playback device could not be opened.
inline constexpr int kAudioInitFailed = 2010;

}  // namespace fa::platform::platform_error
