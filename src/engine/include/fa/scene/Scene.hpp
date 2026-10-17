#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "fa/render/RenderQueue.hpp"
#include "fa/scene/System.hpp"
#include "fa/scene/World.hpp"
#include "fa/time/FrameContext.hpp"

namespace fa::scene {

/// \file
/// A scene is one assembled world: a `World`, the systems that run on it, and the
/// lifecycle callbacks the scene stack drives.
///
/// Splitting "the container" (`World`) from "the assembly" (a scene) is what keeps
/// the ECS usable by tests and tools that do not want a scene at all, and lets a
/// gameplay module define its own scene subclass without touching this module.

/// \brief Lifecycle + update surface the scene stack talks to.
///
/// A scene owns its world, so the extract hook takes only the target queue. The stack
/// calls `onPause` when another scene is pushed on top of this one and `onResume` when
/// that scene is popped; a paused scene is not updated but is still extracted, which
/// is what lets a pause menu sit on top of a frozen world.
class IScene {
public:
    IScene() = default;
    virtual ~IScene() = default;
    IScene(const IScene&) = delete;
    IScene& operator=(const IScene&) = delete;
    IScene(IScene&&) = delete;
    IScene& operator=(IScene&&) = delete;

    /// \brief Stable name for logs, the debug overlay and `SceneStack::find`.
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    virtual void onEnter() {}
    virtual void onExit() {}
    virtual void onPause() {}
    virtual void onResume() {}

    virtual void onFixedUpdate(const time::FrameContext& context) { static_cast<void>(context); }
    virtual void onVariableUpdate(const time::FrameContext& context) { static_cast<void>(context); }
    virtual void onRenderExtract(render::RenderQueue& queue) { static_cast<void>(queue); }
};

/// \brief Ready-made scene: a `World` plus a `SystemPipeline` wired to the hooks.
///
/// Gameplay scenes should derive from this rather than from `IScene`; overriding a
/// hook is then a deliberate act instead of the default. Deriving never hides the
/// world - `world()` and `systems()` stay public, so the smoke app and tests can poke
/// at entities directly.
class BasicScene : public IScene {
public:
    explicit BasicScene(std::string name);

    [[nodiscard]] std::string_view name() const noexcept override;

    [[nodiscard]] World& world() noexcept { return m_world; }
    [[nodiscard]] const World& world() const noexcept { return m_world; }
    [[nodiscard]] SystemPipeline& systems() noexcept { return m_pipeline; }
    [[nodiscard]] const SystemPipeline& systems() const noexcept { return m_pipeline; }

    void onFixedUpdate(const time::FrameContext& context) override;
    void onVariableUpdate(const time::FrameContext& context) override;
    void onRenderExtract(render::RenderQueue& queue) override;

private:
    std::string m_name;
    World m_world;
    SystemPipeline m_pipeline;
};

}  // namespace fa::scene
