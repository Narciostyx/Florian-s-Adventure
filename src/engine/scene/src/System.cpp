#include "fa/scene/System.hpp"

#include "fa/core/Assert.hpp"

namespace fa::scene {

void SystemPipeline::add(std::unique_ptr<ISystem> system) {
    if (system == nullptr) {
        return;
    }
    m_systems.push_back(std::move(system));
}

void SystemPipeline::fixedUpdate(World& world, const time::FrameContext& context) {
    ++m_fixedRuns;
    for (const auto& system : m_systems) {
        system->onFixedUpdate(world, context, m_commands);
        drain(world);
    }
}

void SystemPipeline::variableUpdate(World& world, const time::FrameContext& context) {
    ++m_variableRuns;
    for (const auto& system : m_systems) {
        system->onVariableUpdate(world, context, m_commands);
        drain(world);
    }
}

void SystemPipeline::renderExtract(const World& world, render::RenderQueue& queue) {
    ++m_extractRuns;
    for (const auto& system : m_systems) {
        system->onRenderExtract(world, queue);
    }
}

ISystem* SystemPipeline::at(std::size_t index) noexcept {
    FA_ASSERT(index < m_systems.size(), "system index out of range");
    return m_systems[index].get();
}

const ISystem* SystemPipeline::at(std::size_t index) const noexcept {
    FA_ASSERT(index < m_systems.size(), "system index out of range");
    return m_systems[index].get();
}

SystemPipelineStats SystemPipeline::stats() const noexcept {
    SystemPipelineStats result;
    result.systems = m_systems.size();
    result.fixedRuns = m_fixedRuns;
    result.variableRuns = m_variableRuns;
    result.extractRuns = m_extractRuns;
    result.commandsApplied = m_commandsApplied;
    result.commandsSkipped = m_commandsSkipped;
    return result;
}

void SystemPipeline::clear() noexcept {
    m_systems.clear();
    m_commands.clear();
}

void SystemPipeline::drain(World& world) {
    if (m_commands.empty()) {
        return;
    }
    const CommandBufferStats applied = m_commands.apply(world);
    m_commandsApplied += applied.applied;
    m_commandsSkipped += applied.skipped;
}

}  // namespace fa::scene
