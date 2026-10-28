#pragma once

#include <cstddef>
#include <string_view>

#include "fa/render/RenderQueue.hpp"
#include "fa/scene/System.hpp"
#include "fa/scene/World.hpp"

namespace fa::scene {

/// \file
/// Turns `Transform` + `Sprite` components into `render::DrawItem`s.
///
/// This is the only bridge between the ECS and M06, and it is a free function on
/// purpose: the draw data is pure, so a test can build a world, extract, and assert on
/// the exact queue without a window, a GL context or a renderer. The app is then
/// responsible for the one call that submits the queue.

/// \brief Knobs for one extraction pass. All of them have playable defaults.
struct SpriteExtractOptions {
    /// \brief Skip sprites whose texture handle is invalid instead of drawing them.
    ///
    /// Default true, which is what makes placeholder and not-yet-loaded art a no-op
    /// rather than a giant white quad.
    bool requireTexture = true;
    /// \brief Apply `Transform::scale` and `Transform::rotation`.
    bool applyTransform = true;
    /// \brief Added to every sprite's feet before it becomes the depth key.
    float sortYBias = 0.0f;
};

/// \brief What one extraction pass produced.
struct SpriteExtractStats {
    /// \brief Entities that had both a `Transform` and a `Sprite`.
    std::size_t candidates = 0;
    /// \brief Items actually queued.
    std::size_t submitted = 0;
    /// \brief Sprites dropped because their texture handle was invalid.
    std::size_t skippedNoTexture = 0;
};

/// \brief Appends one `DrawItem` per drawable entity to `queue`.
///
/// Depth comes from the entity's feet (`position.y + size.y`), so a character standing
/// lower on the screen draws in front of one standing higher - the classic top-down
/// rule. `DrawItem::entityId` is set to the entity index, which is unique among living
/// entities and is what the queue uses as its final tie-break.
[[nodiscard]] SpriteExtractStats
extractSprites(const World& world, render::RenderQueue& queue, const SpriteExtractOptions& options = {});

/// \brief A system that does nothing but `extractSprites`.
class SpriteExtractSystem final : public ISystem {
public:
    explicit SpriteExtractSystem(SpriteExtractOptions options = {});
    ~SpriteExtractSystem() override = default;

    [[nodiscard]] std::string_view name() const noexcept override;
    void onRenderExtract(const World& world, render::RenderQueue& queue) override;

    [[nodiscard]] const SpriteExtractStats& lastStats() const noexcept { return m_lastStats; }

private:
    SpriteExtractOptions m_options;
    SpriteExtractStats m_lastStats;
};

}  // namespace fa::scene
