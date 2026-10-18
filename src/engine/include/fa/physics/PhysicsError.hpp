#pragma once

namespace fa::physics {

/// \file
/// Error codes owned by M13.
///
/// `fa/core/Error.hpp` reserves 5000-5999 for Ecs / Physics; M11 took 5000-5007, so
/// collision starts at 5008 and the two modules never collide.
namespace physics_error {

/// \brief A collider or collision layer description was unusable.
inline constexpr int kInvalidCollider = 5008;

/// \brief A tile grid was asked for with a non-positive tile size or extent.
inline constexpr int kInvalidTileSize = 5009;

/// \brief A query or step needed a tile provider and none was configured.
inline constexpr int kNoTileProvider = 5010;

/// \brief Movement was requested for an entity that has no transform.
inline constexpr int kMissingTransform = 5011;

/// \brief A broadphase index was queried before it was built from a world.
inline constexpr int kIndexNotSynced = 5012;

}  // namespace physics_error

}  // namespace fa::physics
