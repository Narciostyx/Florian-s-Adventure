#include "fa/scene/World.hpp"

#include "fa/core/Assert.hpp"

namespace fa::scene {

EntityId World::create() {
    ++m_createdTotal;
    if (!m_freeIndices.empty()) {
        const std::uint32_t index = m_freeIndices.back();
        m_freeIndices.pop_back();
        Slot& slot = m_entities[index];
        slot.alive = true;
        slot.generation += 1;
        if (slot.generation == 0 || slot.generation == EntityId::kPendingGeneration) {
            slot.generation = 1;
        }
        ++m_liveCount;
        return EntityId{index, slot.generation};
    }

    FA_ASSERT(m_entities.size() < 0xFFFFFFFFu, "entity slot pool exhausted");
    m_entities.push_back(Slot{1, true});
    ++m_liveCount;
    return EntityId{static_cast<std::uint32_t>(m_entities.size() - 1), 1};
}

bool World::destroy(EntityId id) {
    if (!isAlive(id)) {
        return false;
    }
    for (auto& entry : m_storages) {
        static_cast<void>(entry.second->remove(id));
    }
    Slot& slot = m_entities[id.index];
    slot.alive = false;
    m_freeIndices.push_back(id.index);
    --m_liveCount;
    ++m_destroyedTotal;
    return true;
}

bool World::isAlive(EntityId id) const noexcept {
    if (!id.isValid() || id.index >= m_entities.size()) {
        return false;
    }
    const Slot& slot = m_entities[id.index];
    return slot.alive && slot.generation == id.generation;
}

WorldStats World::stats() const noexcept {
    WorldStats result;
    result.liveEntities = m_liveCount;
    result.entitySlots = entitySlots();
    result.recycledSlots = m_freeIndices.size();
    result.componentTypes = m_storages.size();
    for (const auto& entry : m_storages) {
        result.components += entry.second->size();
    }
    result.createdTotal = m_createdTotal;
    result.destroyedTotal = m_destroyedTotal;
    return result;
}

void World::clear() noexcept {
    for (auto& entry : m_storages) {
        entry.second->clear();
    }
    m_entities.assign(1, Slot{});
    m_freeIndices.clear();
    m_liveCount = 0;
}

}  // namespace fa::scene
