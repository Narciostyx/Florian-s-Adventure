#pragma once

namespace fa::tilemap::tilemap_error {

/// \file
/// Error codes for M12. The map sits in the world layer next to the ECS and physics,
/// so it continues the 5000 block: 5000-5007 belong to the scene module and
/// 5008-5012 to physics, leaving 5013 onwards for the tile map.

/// \brief Width, height or the cell count is not usable.
inline constexpr int kInvalidDimensions = 5013;

/// \brief The requested tile size is not a positive, sane pixel count.
inline constexpr int kInvalidTileSize = 5014;

/// \brief Two definitions use the same tile id.
inline constexpr int kDuplicateTileId = 5015;

/// \brief A layer is unnamed, duplicated, has a bad render layer or a bad z order.
inline constexpr int kInvalidLayer = 5016;

/// \brief A layer's tile array does not match `width * height`.
inline constexpr int kLayerSizeMismatch = 5017;

/// \brief A layer refers to a tile id that has no definition.
inline constexpr int kUnknownTileId = 5018;

/// \brief The document is not valid JSON.
inline constexpr int kParseFailed = 5019;

/// \brief The document declares a schema version this build does not understand.
inline constexpr int kUnsupportedSchema = 5020;

/// \brief An area or portal has invalid or out-of-map bounds.
inline constexpr int kInvalidRegion = 5021;

/// \brief A required field is missing or has the wrong type.
inline constexpr int kInvalidDocument = 5022;

}  // namespace fa::tilemap::tilemap_error
