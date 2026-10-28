#include "fa/scene/SceneStack.hpp"

#include <utility>

#include "fa/scene/SceneError.hpp"

namespace fa::scene {

SceneStack::~SceneStack() {
    clear();
}

core::Status SceneStack::push(std::unique_ptr<IScene> scene) {
    if (scene == nullptr) {
        return core::errorStatus(scene_error::kNullScene, "SceneStack::push needs a scene");
    }
    if (m_scenes.size() >= kMaxDepth) {
        return core::errorStatus(scene_error::kSceneStackOverflow, "scene stack is full");
    }
    if (!m_scenes.empty()) {
        m_scenes.back()->onPause();
        ++m_pauses;
    }
    m_scenes.push_back(std::move(scene));
    ++m_pushes;
    m_scenes.back()->onEnter();
    return core::okStatus();
}

std::unique_ptr<IScene> SceneStack::pop() {
    if (m_scenes.empty()) {
        return nullptr;
    }
    std::unique_ptr<IScene> popped = std::move(m_scenes.back());
    m_scenes.pop_back();
    ++m_pops;
    popped->onExit();
    if (!m_scenes.empty()) {
        m_scenes.back()->onResume();
        ++m_resumes;
    }
    return popped;
}

core::Status SceneStack::replace(std::unique_ptr<IScene> scene) {
    if (scene == nullptr) {
        return core::errorStatus(scene_error::kNullScene, "SceneStack::replace needs a scene");
    }
    if (m_scenes.empty()) {
        return push(std::move(scene));
    }
    m_scenes.back()->onExit();
    m_scenes.back() = std::move(scene);
    ++m_replacements;
    m_scenes.back()->onEnter();
    return core::okStatus();
}

IScene* SceneStack::top() noexcept {
    return m_scenes.empty() ? nullptr : m_scenes.back().get();
}

const IScene* SceneStack::top() const noexcept {
    return m_scenes.empty() ? nullptr : m_scenes.back().get();
}

IScene* SceneStack::at(std::size_t index) noexcept {
    return index < m_scenes.size() ? m_scenes[index].get() : nullptr;
}

const IScene* SceneStack::at(std::size_t index) const noexcept {
    return index < m_scenes.size() ? m_scenes[index].get() : nullptr;
}

IScene* SceneStack::find(std::string_view name) noexcept {
    for (auto it = m_scenes.rbegin(); it != m_scenes.rend(); ++it) {
        if ((*it)->name() == name) {
            return it->get();
        }
    }
    return nullptr;
}

void SceneStack::clear() noexcept {
    while (!m_scenes.empty()) {
        m_scenes.back()->onExit();
        m_scenes.pop_back();
        ++m_pops;
    }
}

void SceneStack::fixedUpdate(const time::FrameContext& context) {
    if (IScene* current = top(); current != nullptr) {
        current->onFixedUpdate(context);
    }
}

void SceneStack::variableUpdate(const time::FrameContext& context) {
    if (IScene* current = top(); current != nullptr) {
        current->onVariableUpdate(context);
    }
}

void SceneStack::renderExtract(render::RenderQueue& queue) {
    for (const auto& scene : m_scenes) {
        scene->onRenderExtract(queue);
    }
}

SceneStackStats SceneStack::stats() const noexcept {
    SceneStackStats result;
    result.depth = m_scenes.size();
    result.pushes = m_pushes;
    result.pops = m_pops;
    result.replacements = m_replacements;
    result.pauses = m_pauses;
    result.resumes = m_resumes;
    return result;
}

}  // namespace fa::scene
