#include "fa/scene/CommandBuffer.hpp"

namespace fa::scene {

namespace detail {

EntityId resolvePending(EntityId id, const std::unordered_map<std::uint32_t, EntityId>& resolved) noexcept {
    if (!id.isPending()) {
        return id;
    }
    const auto found = resolved.find(id.index);
    return found != resolved.end() ? found->second : EntityId{};
}

bool CreateCommand::run(World& world, std::unordered_map<std::uint32_t, EntityId>& resolved) {
    const EntityId created = world.create();
    resolved[m_placeholder.index] = created;
    return true;
}

bool DestroyCommand::run(World& world, std::unordered_map<std::uint32_t, EntityId>& resolved) {
    const EntityId target = resolvePending(m_target, resolved);
    if (!target.isValid()) {
        return false;
    }
    return world.destroy(target);
}

}  // namespace detail

EntityId CommandBuffer::create() {
    ++m_nextPlaceholder;
    const EntityId placeholder{m_nextPlaceholder, EntityId::kPendingGeneration};
    m_commands.push_back(std::make_unique<detail::CreateCommand>(placeholder));
    return placeholder;
}

void CommandBuffer::destroy(EntityId target) noexcept {
    m_commands.push_back(std::make_unique<detail::DestroyCommand>(target));
}

CommandBufferStats CommandBuffer::apply(World& world) {
    CommandBufferStats result;
    for (const auto& command : m_commands) {
        const CommandKind kind = command->kind();
        const bool ran = command->run(world, m_resolved);
        if (!ran) {
            ++result.skipped;
            continue;
        }
        ++result.applied;
        switch (kind) {
            case CommandKind::Create:
                ++result.created;
                break;
            case CommandKind::Destroy:
                ++result.destroyed;
                break;
            case CommandKind::AddComponent:
                ++result.componentsAdded;
                break;
            case CommandKind::RemoveComponent:
                ++result.componentsRemoved;
                break;
        }
    }
    m_appliedTotal += result.applied;
    m_commands.clear();
    return result;
}

EntityId CommandBuffer::resolve(EntityId id) const noexcept {
    if (!id.isPending()) {
        return id;
    }
    const auto found = m_resolved.find(id.index);
    return found != m_resolved.end() ? found->second : EntityId{};
}

void CommandBuffer::clear() noexcept {
    m_commands.clear();
    m_resolved.clear();
}

}  // namespace fa::scene
