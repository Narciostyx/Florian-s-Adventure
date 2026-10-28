#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "fa/render/RenderQueue.hpp"
#include "fa/scene/CommandBuffer.hpp"
#include "fa/scene/World.hpp"
#include "fa/time/FrameContext.hpp"

namespace fa::scene {

/// \file
/// Systems and the scheduler that orders them.
///
/// A system is behaviour: it reads components out of a `World` and writes them back.
/// It never owns entities and never stores a `World&` (conventions section 9), which
/// means the same system instance could serve another world - and, more usefully,
/// that a system cannot outlive its world through a dangling reference.

/// \brief One step of behaviour.
///
/// The three hooks line up with the frame loop in M04: `onFixedUpdate` runs at the
/// fixed 60 Hz rate, `onVariableUpdate` once per frame with the interpolation alpha,
/// and `onRenderExtract` turns the world into draw items.
///
/// `onRenderExtract` deliberately takes a `render::RenderQueue` and not an
/// `IRenderer`: extraction produces plain data, so a test can assert the exact items
/// a world produces without a window or a GL context, and the renderer keeps its
/// single submit path.
///
/// Every hook receives the shared `CommandBuffer`; structural changes queued there are
/// applied right after the hook returns (see `SystemPipeline`).
class ISystem {
public:
    ISystem() = default;
    virtual ~ISystem() = default;
    ISystem(const ISystem&) = delete;
    ISystem& operator=(const ISystem&) = delete;
    ISystem(ISystem&&) = delete;
    ISystem& operator=(ISystem&&) = delete;

    /// \brief Stable name for logs and the debug overlay. Never empty.
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    virtual void onFixedUpdate(World& world, const time::FrameContext& context, CommandBuffer& commands) {
        static_cast<void>(world);
        static_cast<void>(context);
        static_cast<void>(commands);
    }

    virtual void onVariableUpdate(World& world, const time::FrameContext& context, CommandBuffer& commands) {
        static_cast<void>(world);
        static_cast<void>(context);
        static_cast<void>(commands);
    }

    virtual void onRenderExtract(const World& world, render::RenderQueue& queue) {
        static_cast<void>(world);
        static_cast<void>(queue);
    }
};

/// \brief What the pipeline has run so far.
struct SystemPipelineStats {
    std::size_t systems = 0;
    std::uint64_t fixedRuns = 0;
    std::uint64_t variableRuns = 0;
    std::uint64_t extractRuns = 0;
    std::uint64_t commandsApplied = 0;
    std::uint64_t commandsSkipped = 0;
};

/// \brief Ordered list of systems, one shared command buffer, three entry points.
///
/// Execution order is registration order, for all three hooks. That order is a real
/// dependency - "move before collide" is expressed only by the order the composition
/// root adds systems in - so scenes must add systems in a documented order.
class SystemPipeline {
public:
    SystemPipeline() = default;
    SystemPipeline(const SystemPipeline&) = delete;
    SystemPipeline& operator=(const SystemPipeline&) = delete;
    SystemPipeline(SystemPipeline&&) = delete;
    SystemPipeline& operator=(SystemPipeline&&) = delete;
    ~SystemPipeline() = default;

    /// \brief Appends a system. A null pointer is ignored.
    void add(std::unique_ptr<ISystem> system);

    /// \brief Runs every `onFixedUpdate`, draining commands after each system.
    void fixedUpdate(World& world, const time::FrameContext& context);

    /// \brief Runs every `onVariableUpdate`, draining commands after each system.
    void variableUpdate(World& world, const time::FrameContext& context);

    /// \brief Runs every `onRenderExtract`. Read-only pass, so no command draining.
    void renderExtract(const World& world, render::RenderQueue& queue);

    [[nodiscard]] std::size_t size() const noexcept { return m_systems.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_systems.empty(); }
    [[nodiscard]] ISystem* at(std::size_t index) noexcept;
    [[nodiscard]] const ISystem* at(std::size_t index) const noexcept;

    [[nodiscard]] CommandBuffer& commands() noexcept { return m_commands; }
    [[nodiscard]] const CommandBuffer& commands() const noexcept { return m_commands; }

    [[nodiscard]] SystemPipelineStats stats() const noexcept;

    /// \brief Removes every system and drops queued commands.
    void clear() noexcept;

private:
    void drain(World& world);

    std::vector<std::unique_ptr<ISystem>> m_systems;
    CommandBuffer m_commands;
    std::uint64_t m_fixedRuns = 0;
    std::uint64_t m_variableRuns = 0;
    std::uint64_t m_extractRuns = 0;
    std::uint64_t m_commandsApplied = 0;
    std::uint64_t m_commandsSkipped = 0;
};

}  // namespace fa::scene
