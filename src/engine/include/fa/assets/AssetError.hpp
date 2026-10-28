#pragma once

#include <fa/core/Error.hpp>

namespace fa::assets::asset_error {

/// \brief An operation needed a manager and none is available, or the manager was
/// built without the file system or catalog it requires.
inline constexpr int kNotInitialized = 3000;

/// \brief The catalog has no entry for the requested id.
inline constexpr int kUnknownAsset = 3001;

/// \brief Reading the source bytes failed. The failing `platform::Error` code stays
/// inside the message so callers do not need a second error channel.
inline constexpr int kLoadFailed = 3002;

/// \brief A decoder claimed the file but could not turn the bytes into pixels.
inline constexpr int kDecodeFailed = 3003;

/// \brief The catalog JSON is malformed, or an entry is missing a required field.
inline constexpr int kCatalogParseFailed = 3004;

/// \brief An image description is unusable: non-positive size, or fewer pixel
/// bytes than the size requires.
inline constexpr int kInvalidImage = 3005;

/// \brief The catalog entry names a kind this manager does not handle.
inline constexpr int kUnsupportedKind = 3006;

/// \brief The registered `ITextureUploader` refused the image.
inline constexpr int kUploadFailed = 3007;

}  // namespace fa::assets::asset_error
