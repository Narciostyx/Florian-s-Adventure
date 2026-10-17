#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "fa/core/Result.hpp"
#include "fa/render/RenderQueue.hpp"
#include "fa/scene/Scene.hpp"
#include "fa/time/FrameContext.hpp"

namespace fa::scene {

/// \file
/// A stack of scenes: the bottom one is the world being played, anything above it is
/// an overlay (pause menu, dialog, inventory).
///
/// Only the top scene updates. Every scene on the stack is extracted, bottom first, so
/// an overlay draws on top of a world that is still visible but frozen. That single
/// rule replaces a per-scene "should I update" flag and keeps the pause behaviour
/// impossible to get wrong.

/// \brief What the stack has done so far, for logs and tests.
struct SceneStackStats {
    std::size_t depth = 0;
    std::uint64_t pushes = 0;
    std::uint64_t pops = 0;
    std::uint64_t replacements = 0;
    std::uint64_t pauses = 0;
    std::uint64_t resumes = 0;
};

/// \brief Owns its scenes and drives them.
class SceneStack {
public:
    /// \brief Hard cap, so a runaway push loop fails loudly instead of eating memory.
    static constexpr std::size_t kMaxDepth = 16;

    SceneStack() = default;
    SceneStack(const SceneStack&) = delete;
    SceneStack& operator=(const SceneStack&) = delete;
    SceneStack(SceneStack&&) = delete;
    SceneStack& operator=(SceneStack&&) = delete;
    ~SceneStack();

    /// \brief Pushes a scene on top, pausing the one it covers and calling its `onEnter`.
    ///
    /// A null scene and a stack already at `kMaxDepth` both fail with a scene error
    /// code from `SceneError.hpp`; neither leaves the stack changed.
    core::Status push(std::unique_ptr<IScene> scene);

    /// \brief Pops the top scene, calling `onExit` then resuming whatever was uncovered.
    /// \return The popped scene (caller keeps it alive as long as it likes), or null
    ///         when the stack was empty.
    std::unique_ptr<IScene> pop();

    /// \brief Exits the top scene and puts `scene` in its place, keeping the depth.
    ///
    /// With an empty stack this behaves like `push`. The replacement is a different
    /// scene, so no `onResume` follows - this is the "load level 2" path, not "close
    /// the menu".
    core::Status replace(std::unique_ptr<IScene> scene);

    [[nodiscard]] IScene* top() noexcept;
    [[nodiscard]] const IScene* top() const noexcept;

    /// \brief Scene at `index`, where 0 is the bottom of the stack.
    [[nodiscard]] IScene* at(std::size_t index) noexcept;
    [[nodiscard]] const IScene* at(std::size_t index) const noexcept;

    /// \brief Scene whose `name()` matches, or null. Search runs top-down.
    [[nodiscard]] IScene* find(std::string_view name) noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return m_scenes.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_scenes.empty(); }

    /// \brief Pops every scene, top-down, calling `onExit` on each.
    void clear() noexcept;

    /// \brief Runs `onFixedUpdate` on the top scene only.
    void fixedUpdate(const time::FrameContext& context);
    /// \brief Runs `onVariableUpdate` on the top scene only.
    void variableUpdate(const time::FrameContext& context);
    /// \brief Runs `onRenderExtract` on every scene, bottom first.
    void renderExtract(render::RenderQueue& queue);

    [[nodiscard]] SceneStackStats stats() const noexcept;

private:
    std::vector<std::unique_ptr<IScene>> m_scenes;
    std::uint64_t m_pushes = 0;
    std::uint64_t m_pops = 0;
    std::uint64_t m_replacements = 0;
    std::uint64_t m_pauses = 0;
    std::uint64_t m_resumes = 0;
};

}  // namespace fa::scene
