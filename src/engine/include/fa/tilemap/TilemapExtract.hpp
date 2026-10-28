#pragma once

#include <cstddef>
#include <string_view>

#include "fa/core/Math.hpp"
#include "fa/render/RenderQueue.hpp"
#include "fa/scene/System.hpp"
#include "fa/tilemap/Tilemap.hpp"

namespace fa::tilemap {

/// \file
/// Turns a `Tilemap` into `render::DrawItem`s.
///
/// Like M11's sprite extractor this produces plain data and never takes an
/// `IRenderer&`, so the exact draw list can be asserted in a headless test. Culling
/// is opt-in through a world-space rectangle; the default extracts the whole map,
/// which is the right behaviour for small maps and for tests.

/// \brief Knobs for one tile extraction pass.
struct TileExtractOptions {
    /// \brief World-space rectangle to cull against; ignored unless `cullToViewBounds`.
    core::Rect viewBounds{};
    /// \brief Skip cells outside `viewBounds`. Default false = draw the whole map.
    bool cullToViewBounds = false;
    /// \brief Skip definitions whose texture was never bound.
    ///
    /// True is what makes a map loadable before the atlas exists: unbound tiles are
    /// simply absent instead of a field of white quads.
    bool skipWithoutTexture = true;
};

/// \brief What one extraction pass did.
struct TileExtractStats {
    /// \brief Visible layers that were walked.
    std::size_t layers = 0;
    /// \brief Cells visited after culling (all layers).
    std::size_t candidates = 0;
    /// \brief Cells outside the view rectangle.
    std::size_t culled = 0;
    /// \brief Cells whose tile id was zero.
    std::size_t skippedEmpty = 0;
    /// \brief Cells whose definition or texture was missing.
    std::size_t skippedNoTexture = 0;
    /// \brief Draw items appended to the queue.
    std::size_t submitted = 0;
};

/// \brief Appends one `DrawItem` per visible, non-empty tile.
///
/// Layers are walked in ascending `zOrder`, ties in insertion order. The queue's
/// final tie-break is `entityId`, so items submitted later in that walk draw on top
/// when two layers share a `render::RenderLayer` and a row.
[[nodiscard]] TileExtractStats
extractTiles(const Tilemap& map, render::RenderQueue& queue, const TileExtractOptions& options = {});

/// \brief A system that extracts a borrowed map during `onRenderExtract`.
class TilemapExtractSystem final : public scene::ISystem {
public:
    /// \brief \param map Borrowed; it must outlive the system, exactly like `PhysicsSystem`'s tiles.
    explicit TilemapExtractSystem(const Tilemap& map, TileExtractOptions options = {});
    ~TilemapExtractSystem() override = default;

    [[nodiscard]] std::string_view name() const noexcept override;
    void onRenderExtract(const scene::World& world, render::RenderQueue& queue) override;

    /// \brief Sets the camera's world-space rectangle and enables culling.
    ///
    /// An empty rect disables culling, so a scene without a camera still draws.
    void setViewBounds(core::Rect bounds) noexcept;
    void clearViewBounds() noexcept;

    [[nodiscard]] const TileExtractOptions& options() const noexcept { return m_options; }
    void setOptions(const TileExtractOptions& options) noexcept { m_options = options; }

    [[nodiscard]] const TileExtractStats& lastStats() const noexcept { return m_lastStats; }

private:
    const Tilemap* m_map = nullptr;
    TileExtractOptions m_options;
    TileExtractStats m_lastStats;
};

}  // namespace fa::tilemap
