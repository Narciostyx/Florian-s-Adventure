#pragma once

#include <fa/core/Error.hpp>

namespace fa::render::render_error {

/// \brief A required OpenGL entry point could not be resolved.
inline constexpr int kGlFunctionMissing = 4000;

/// \brief A shader failed to compile.
inline constexpr int kShaderCompileFailed = 4001;

/// \brief A shader program failed to link.
inline constexpr int kShaderLinkFailed = 4002;

/// \brief Texture creation was rejected, or a texture handle is stale.
inline constexpr int kTextureCreationFailed = 4003;

/// \brief An operation needed a live renderer and one is not initialized.
inline constexpr int kNotInitialized = 4004;

/// \brief A texture handle was used after the texture was destroyed.
inline constexpr int kUnknownTexture = 4005;

}  // namespace fa::render::render_error
