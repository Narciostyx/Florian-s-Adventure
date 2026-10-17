#pragma once

namespace fa::render {

/// \brief Internal horizontal resolution every draw call is expressed in.
///
/// The window is only ever an integer-scaled, letterboxed view of this surface
/// (docs/architecture/pixel-metrics.md), so gameplay and camera math never see
/// window pixels.
inline constexpr int kVirtualWidth = 640;

/// \brief Internal vertical resolution; see kVirtualWidth.
inline constexpr int kVirtualHeight = 360;

/// \brief Edge length of one world tile in virtual pixels.
///
/// Lives here because the camera, the sprite batching and the texture atlas all
/// have to agree on it; M11 reads it rather than redefining it.
inline constexpr int kTileSize = 16;

/// \brief Quads one frame may batch before the renderer flushes early.
///
/// 4096 quads is 16 KiB of vertices and 96 KiB of indices, which stays inside a
/// single dynamic buffer upload for a full screen of 16x16 tiles.
inline constexpr int kDefaultMaxQuads = 4096;

/// \brief Upper bound on live textures, so handles stay small and dense.
inline constexpr int kMaxTextures = 4096;

/// \brief Largest accepted texture edge in pixels.
inline constexpr int kMaxTextureSize = 16384;

}  // namespace fa::render
