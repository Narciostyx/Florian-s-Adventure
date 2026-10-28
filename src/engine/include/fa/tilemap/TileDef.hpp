#pragma once

#include <string>

#include "fa/core/Math.hpp"
#include "fa/render/RenderTypes.hpp"
#include "fa/tilemap/TileId.hpp"

namespace fa::tilemap {

/// \brief Static description of one tile kind.
///
/// `texture` is a plain `render::TextureHandle`, not an `assets::AssetHandle`, for
/// the same reason `scene::Sprite` uses one: the map stays a value type with no
/// destructor and `fa_tilemap` never links the asset manager. A JSON loader fills
/// `textureName`; the composition root resolves that name and calls
/// `Tilemap::setTileTexture()`.
struct TileDef {
    /// \brief Definition key; never zero.
    TileId id{};
    /// \brief Human-readable name used by editors and logs.
    std::string name;
    /// \brief Asset id or catalog name read from the map document, if any.
    std::string textureName;
    /// \brief Resolved GPU texture. An invalid handle means "do not draw".
    render::TextureHandle texture{};
    /// \brief Sub-rectangle of `texture`; empty means the whole texture.
    core::Rect source{};
    /// \brief Whether a body may stand on this tile at all.
    bool walkable = true;
    /// \brief Whether this tile blocks movement for bodies that collide with the world.
    ///
    /// Separate from `walkable` because water is neither walkable nor a wall: a
    /// swimmer or a bridge can enter it while a land body cannot.
    bool solid = false;
    /// \brief Whether this tile is water; gameplay reads this for swimming and watering.
    bool water = false;
    /// \brief Whether a hoe may turn this tile into farmland.
    bool tillable = false;
    /// \brief Free-form content tag (`grass`, `path`, `crop_zone`, ...).
    std::string tag;
};

}  // namespace fa::tilemap
