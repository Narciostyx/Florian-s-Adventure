#include "fa/scene/Scene.hpp"

#include <utility>

namespace fa::scene {

BasicScene::BasicScene(std::string name) : m_name(std::move(name)) {
    if (m_name.empty()) {
        m_name = "scene";
    }
}

std::string_view BasicScene::name() const noexcept {
    return m_name;
}

void BasicScene::onFixedUpdate(const time::FrameContext& context) {
    m_pipeline.fixedUpdate(m_world, context);
}

void BasicScene::onVariableUpdate(const time::FrameContext& context) {
    m_pipeline.variableUpdate(m_world, context);
}

void BasicScene::onRenderExtract(render::RenderQueue& queue) {
    m_pipeline.renderExtract(m_world, queue);
}

}  // namespace fa::scene
