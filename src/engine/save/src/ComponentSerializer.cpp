#include "fa/save/ComponentSerializer.hpp"

#include <utility>

namespace fa::save {

bool ComponentSerializerRegistry::add(std::unique_ptr<IComponentSerializer> serializer) {
    if (!serializer || serializer->name().empty()) {
        return false;
    }
    for (const std::unique_ptr<IComponentSerializer>& existing : m_serializers) {
        if (existing->name() == serializer->name()) {
            return false;
        }
    }
    m_serializers.push_back(std::move(serializer));
    return true;
}

const IComponentSerializer* ComponentSerializerRegistry::at(std::size_t index) const noexcept {
    return index < m_serializers.size() ? m_serializers[index].get() : nullptr;
}

void ComponentSerializerRegistry::saveAll(const scene::World& world, SaveArchive& out) const {
    SaveArchive result = SaveArchive::object();
    for (const std::unique_ptr<IComponentSerializer>& serializer : m_serializers) {
        SaveArchive section = SaveArchive::object();
        serializer->save(world, section);
        result.setChild(serializer->name(), std::move(section));
    }
    out = std::move(result);
}

void ComponentSerializerRegistry::loadAll(scene::World& world, const SaveArchive& in) const {
    for (const std::unique_ptr<IComponentSerializer>& serializer : m_serializers) {
        if (!in.has(serializer->name())) {
            continue;
        }
        serializer->load(world, in.child(serializer->name()));
    }
}

}  // namespace fa::save
