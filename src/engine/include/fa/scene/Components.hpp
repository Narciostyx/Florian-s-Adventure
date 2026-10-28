#pragma once

#include <cstdint>

#include "fa/core/Math.hpp"
#include "fa/render/RenderTypes.hpp"

namespace fa::scene {

/// \file
/// The two components every drawable needs, and nothing else.
///
/// Per `docs/architecture/conventions.md` section 9 a component is plain data: no
/// virtual functions, no hand-written constructor or destructor, no owning pointers.
/// Anything that needs code lives in a system. Gameplay components (crops, animals,
/// stamina) land in their own module and are declared next to the systems that read
/// them, not here.

/// \brief Where an entity sits in world pixels, and how it is scaled and rotated.
struct Transform {
    /// \brief World-space position in pixels, relative to the scene origin.
    core::Vec2 position{0.0f, 0.0f};
    /// \brief Render scale. Sprites are drawn at `Sprite::size * scale`.
    core::Vec2 scale{1.0f, 1.0f};
    /// \brief Rotation in radians, clockwise around the centre - same unit and
    /// direction as `render::DrawItem::rotation`, so extraction copies it verbatim.
    /// Only multiples of half pi keep the pixel grid intact.
    float rotation = 0.0f;
};

/// \brief What to draw, in the terms the renderer already speaks.
///
/// Holding a `render::TextureHandle` rather than an `assets::AssetHandle` is
/// deliberate: the handle is a plain id with no destructor, so a scene never keeps a
/// texture alive and `fa_scene` never has to link `fa_assets`. Whoever loaded the
/// texture owns that lifetime - in the game that is the asset manager, which outlives
/// every scene.
struct Sprite {
    /// \brief Texture to sample. An invalid handle means "skip", not "draw white".
    render::TextureHandle texture;
    /// \brief Destination size in pixels, before `Transform::scale`.
    core::Vec2 size{16.0f, 16.0f};
    /// \brief Sub-rectangle of the texture. Defaults to the whole thing.
    core::Rect source{0.0f, 0.0f, 0.0f, 0.0f};
    /// \brief Multiplicative colour, straight alpha. Placeholder art uses this as its colour.
    core::Color tint{1.0f, 1.0f, 1.0f, 1.0f};
    /// \brief Which render pass this belongs to.
    render::RenderLayer layer = render::RenderLayer::Actors;
    /// \brief Extra draw-order bias in pixels, added to the entity's feet.
    float sortYBias = 0.0f;
    bool flipX = false;
    bool flipY = false;
    /// \brief Draw order inside one layer and one depth key. 0 means "use the entity index".
    std::uint32_t entityOverride = 0;
};

}  // namespace fa::scene
