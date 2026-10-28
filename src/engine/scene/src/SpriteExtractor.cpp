#include "fa/scene/SpriteExtractor.hpp"

#include "fa/scene/Components.hpp"

namespace fa::scene {

SpriteExtractStats extractSprites(const World& world, render::RenderQueue& queue, const SpriteExtractOptions& options) {
    SpriteExtractStats stats;
    world.each<Transform, Sprite>([&](EntityId id, const Transform& transform, const Sprite& sprite) {
        ++stats.candidates;
        if (options.requireTexture && !sprite.texture.isValid()) {
            ++stats.skippedNoTexture;
            return;
        }

        render::DrawItem item;
        item.texture = sprite.texture;
        item.position = transform.position;
        item.size = sprite.size;
        if (options.applyTransform) {
            item.size.x *= transform.scale.x;
            item.size.y *= transform.scale.y;
            item.rotation = transform.rotation;
        }
        item.source = sprite.source;
        item.tint = sprite.tint;
        item.flipX = sprite.flipX;
        item.flipY = sprite.flipY;
        item.layer = sprite.layer;
        item.sortY = transform.position.y + item.size.y + sprite.sortYBias + options.sortYBias;
        item.entityId = sprite.entityOverride != 0 ? sprite.entityOverride : id.index;
        queue.submit(item);
        ++stats.submitted;
    });
    return stats;
}

SpriteExtractSystem::SpriteExtractSystem(SpriteExtractOptions options) : m_options(options) {}

std::string_view SpriteExtractSystem::name() const noexcept {
    return "SpriteExtractSystem";
}

void SpriteExtractSystem::onRenderExtract(const World& world, render::RenderQueue& queue) {
    m_lastStats = extractSprites(world, queue, m_options);
}

}  // namespace fa::scene
