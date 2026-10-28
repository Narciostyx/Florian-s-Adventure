#pragma once

#include <string>

#include "fa/tilemap/Tilemap.hpp"

namespace fa::tilemap {

/// \file
/// Map document writer. Symmetric with `loadTilemapFromJson()` so the editor can
/// save the exact format the runtime loads.

/// \brief Serializes \p map to the map document JSON shape.
[[nodiscard]] std::string saveTilemapToJson(const Tilemap& map);

}  // namespace fa::tilemap
