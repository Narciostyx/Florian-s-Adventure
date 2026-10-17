#pragma once

#include <fa/core/Error.hpp>

namespace fa::render::render_error {

/// \brief A required OpenGL entry point could not be resolved.
inline constexpr int kGlFunctionMissing = 5000;

/// \brief A shader failed to compile.
inline constexpr int kShaderCompileFailed = 5001;

/// \brief A shader program failed to link.
inline constexpr int kShaderLinkFailed = 5002;

/// \brief Texture creation was rejected, or a texture handle is stale.
inline constexpr int kTextureCreationFailed = 5003;

/// \brief An operation needed a live renderer and one is not initialized.
inline constexpr int kNotInitialized = 5004;

/// \brief A texture handle was used after the texture was destroyed.
inline constexpr int kUnknownTexture = 5005;

}  // namespace fa::render::render_error
