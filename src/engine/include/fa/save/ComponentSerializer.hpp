#pragma once

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

#include <fa/save/SaveArchive.hpp>

namespace fa::scene {
class World;
}

namespace fa::save {

/// \brief Writes one component type to/from a save section.
///
/// Implementations live next to their component (or in the composition root) and are
/// registered at startup. The name is the JSON key under the world section.
class IComponentSerializer {
public:
    virtual ~IComponentSerializer() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    virtual void save(const scene::World& world, SaveArchive& out) const = 0;
    virtual void load(scene::World& world, const SaveArchive& in) const = 0;
};

/// \brief Ordered serializer list with duplicate-name rejection.
class ComponentSerializerRegistry {
public:
    [[nodiscard]] bool add(std::unique_ptr<IComponentSerializer> serializer);
    [[nodiscard]] std::size_t size() const noexcept { return m_serializers.size(); }
    [[nodiscard]] const IComponentSerializer* at(std::size_t index) const noexcept;

    /// \brief Replaces \p out with an object keyed by serializer name.
    void saveAll(const scene::World& world, SaveArchive& out) const;

    /// \brief Calls each serializer whose name exists in \p in.
    void loadAll(scene::World& world, const SaveArchive& in) const;

private:
    std::vector<std::unique_ptr<IComponentSerializer>> m_serializers;
};

}  // namespace fa::save
