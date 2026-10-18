#pragma once

#include <string_view>

#include "fa/core/Result.hpp"
#include "fa/tilemap/Tilemap.hpp"

namespace fa::tilemap {

/// \file
/// Map documents as JSON.
///
/// The document is content, not runtime state: it names tile kinds, layers, areas,
/// portals and ambient values. Texture names stay unbound so this module never links
/// the asset manager; the composition root loads the atlas and calls
/// `Tilemap::setTileTexture()`.

/// \brief Parses one map document.
///
/// The accepted shape is documented in `docs/modules/12-tilemap-and-world.md`:
/// `schema_version`, `width`, `height`, `tile_size`, `origin`, `outside_solid`,
/// `tiles`, `layers`, and optional `areas`, `portals` and `ambient`. Layer data is a
/// row-major flat array of tile ids and must contain exactly `width * height`
/// entries. A tile id used by a layer but not declared in `tiles` is an error.
///
/// \return `tilemap_error::kParseFailed` when the text is not JSON, and the other
/// `kInvalid*` / `kUnsupportedSchema` codes for structural problems.
[[nodiscard]] core::Result<Tilemap> loadTilemapFromJson(std::string_view json);

}  // namespace fa::tilemap
