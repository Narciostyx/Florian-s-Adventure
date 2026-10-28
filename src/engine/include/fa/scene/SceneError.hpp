#pragma once

namespace fa::scene {

/// \file
/// Error codes owned by M11. The 5000-5999 range is reserved for Ecs / Physics by
/// `fa/core/Error.hpp`, so scenes and the future collision module never collide.
namespace scene_error {

inline constexpr int kNotInitialized = 5000;
inline constexpr int kInvalidEntity = 5001;
inline constexpr int kEntityNotAlive = 5002;
inline constexpr int kComponentMissing = 5003;
inline constexpr int kInvalidCommand = 5004;
inline constexpr int kNullScene = 5005;
inline constexpr int kSceneStackOverflow = 5006;
inline constexpr int kSceneNotOnStack = 5007;

}  // namespace scene_error

}  // namespace fa::scene
