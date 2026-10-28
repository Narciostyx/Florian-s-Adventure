#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "fa/core/Assert.hpp"
#include "fa/scene/ComponentType.hpp"
#include "fa/scene/EntityId.hpp"
#include "fa/scene/SparseSet.hpp"

namespace fa::scene {

/// \file
/// The entity container: slot pool, generation recycling, and one `SparseSet` per
/// component type.
///
/// `World` owns entities and components and nothing else - no rendering, no input, no
/// update order. Systems do the work (see `System.hpp`); the node that assembles them
/// is a scene (see `Scene.hpp`). Keeping `World` passive is what makes it testable
/// without a GL context or a frame loop.

/// \brief Snapshot for the debug overlay and the smoke summary.
struct WorldStats {
    std::size_t liveEntities = 0;
    std::size_t entitySlots = 0;
    std::size_t recycledSlots = 0;
    std::size_t componentTypes = 0;
    std::size_t components = 0;
    std::uint64_t createdTotal = 0;
    std::uint64_t destroyedTotal = 0;
};

namespace detail {

/// \brief Type-erased view of one `SparseSet<T>`, so `World` can walk every storage
/// when an entity dies without knowing the component types.
class StorageBase {
public:
    StorageBase() = default;
    virtual ~StorageBase() = default;
    StorageBase(const StorageBase&) = delete;
    StorageBase& operator=(const StorageBase&) = delete;
    StorageBase(StorageBase&&) = delete;
    StorageBase& operator=(StorageBase&&) = delete;

    [[nodiscard]] virtual ComponentTypeId typeId() const noexcept = 0;
    [[nodiscard]] virtual std::size_t size() const noexcept = 0;
    [[nodiscard]] virtual bool has(EntityId id) const noexcept = 0;
    [[nodiscard]] virtual bool remove(EntityId id) noexcept = 0;
    virtual void clear() noexcept = 0;
};

template <typename T>
class Storage final : public StorageBase {
public:
    [[nodiscard]] ComponentTypeId typeId() const noexcept override { return componentTypeIdOf<T>(); }
    [[nodiscard]] std::size_t size() const noexcept override { return set.size(); }
    [[nodiscard]] bool has(EntityId id) const noexcept override { return set.has(id); }
    [[nodiscard]] bool remove(EntityId id) noexcept override { return set.remove(id); }
    void clear() noexcept override { set.clear(); }

    SparseSet<T> set;
};

}  // namespace detail

/// \brief Owns every entity and component in one scene.
///
/// Entity slots are recycled: a destroyed index goes on a free list and comes back
/// with a higher generation, so old handles never resolve. No pointer into the
/// component arrays survives a create or a destroy - component references are only
/// valid for the duration of one system hook.
class World {
public:
    World() = default;
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&&) = delete;
    World& operator=(World&&) = delete;
    ~World() = default;

    /// \brief Creates an entity, reusing a freed slot when one is available.
    EntityId create();

    /// \brief Destroys `id` and every component it owns.
    /// \return false when `id` was already dead or never valid.
    bool destroy(EntityId id);

    /// \brief Whether `id` still refers to a living entity in this world.
    [[nodiscard]] bool isAlive(EntityId id) const noexcept;

    /// \brief Attaches `value`, replacing any component the entity already had.
    /// \return Reference valid until the next structural change.
    ///
    /// Precondition: `isAlive(id)`. Adding to a dead handle is a caller bug, so it
    /// asserts instead of silently resurrecting a slot from another scene era.
    template <typename T>
    T& add(EntityId id, T value = {}) {
        FA_ASSERT(isAlive(id), "add() needs a live entity");
        return storage<T>().add(id, std::move(value));
    }

    template <typename T>
    [[nodiscard]] T* tryGet(EntityId id) noexcept {
        detail::Storage<T>* found = findStorage<T>();
        return found != nullptr ? found->set.tryGet(id) : nullptr;
    }

    template <typename T>
    [[nodiscard]] const T* tryGet(EntityId id) const noexcept {
        const detail::Storage<T>* found = findStorage<T>();
        return found != nullptr ? found->set.tryGet(id) : nullptr;
    }

    template <typename T>
    [[nodiscard]] bool has(EntityId id) const noexcept {
        const detail::Storage<T>* found = findStorage<T>();
        return found != nullptr && found->set.has(id);
    }

    /// \brief Detaches the component. \return false when the entity had none.
    template <typename T>
    bool remove(EntityId id) noexcept {
        detail::Storage<T>* found = findStorage<T>();
        return found != nullptr ? found->set.remove(id) : false;
    }

    /// \brief The storage for `T`, created on first use.
    template <typename T>
    SparseSet<T>& storage() {
        const ComponentTypeId type = componentTypeIdOf<T>();
        const auto existing = m_storages.find(type);
        if (existing != m_storages.end()) {
            return static_cast<detail::Storage<T>*>(existing->second.get())->set;
        }
        auto created = std::make_unique<detail::Storage<T>>();
        detail::Storage<T>* raw = created.get();
        m_storages.emplace(type, std::move(created));
        return raw->set;
    }

    /// \brief Read-only access that does *not* create the storage.
    ///
    /// Returns an empty set when nothing was ever added for `T`, which lets callers
    /// stay `const` without paying for a mutable registry.
    template <typename T>
    [[nodiscard]] const SparseSet<T>& storage() const noexcept {
        static const SparseSet<T> kEmpty;
        const detail::Storage<T>* found = findStorage<T>();
        return found != nullptr ? found->set : kEmpty;
    }

    /// \brief Runs `fn(EntityId, T&...)` for every entity that has all of `Ts`.
    ///
    /// Iterates the entity slot array rather than one component array so the visit
    /// order is stable across runs, which keeps the render queue deterministic.
    /// Entities created during the walk may be visited; entities destroyed during it
    /// are skipped because the liveness flag is re-read every step.
    template <typename... Ts, typename Fn>
    void each(Fn&& fn) {
        static_assert(sizeof...(Ts) > 0, "each() needs at least one component type");
        const std::tuple<detail::Storage<Ts>*...> storages{findStorage<Ts>()...};
        bool missing = false;
        std::apply([&missing](auto*... found) { ((missing = missing || (found == nullptr)), ...); }, storages);
        if (missing) {
            return;
        }
        for (std::size_t index = 1; index < m_entities.size(); ++index) {
            if (!m_entities[index].alive) {
                continue;
            }
            const EntityId id{static_cast<std::uint32_t>(index), m_entities[index].generation};
            std::tuple<Ts*...> values{std::get<detail::Storage<Ts>*>(storages)->set.tryGet(id)...};
            bool complete = true;
            std::apply([&complete](auto*... value) { ((complete = complete && (value != nullptr)), ...); }, values);
            if (!complete) {
                continue;
            }
            std::apply([&fn, id](auto*... value) { fn(id, *value...); }, values);
        }
    }

    /// \brief Read-only `each`, for extraction passes that must not mutate the world.
    template <typename... Ts, typename Fn>
    void each(Fn&& fn) const {
        static_assert(sizeof...(Ts) > 0, "each() needs at least one component type");
        const std::tuple<const detail::Storage<Ts>*...> storages{findStorage<Ts>()...};
        bool missing = false;
        std::apply([&missing](auto*... found) { ((missing = missing || (found == nullptr)), ...); }, storages);
        if (missing) {
            return;
        }
        for (std::size_t index = 1; index < m_entities.size(); ++index) {
            if (!m_entities[index].alive) {
                continue;
            }
            const EntityId id{static_cast<std::uint32_t>(index), m_entities[index].generation};
            std::tuple<const Ts*...> values{std::get<const detail::Storage<Ts>*>(storages)->set.tryGet(id)...};
            bool complete = true;
            std::apply([&complete](auto*... value) { ((complete = complete && (value != nullptr)), ...); }, values);
            if (!complete) {
                continue;
            }
            std::apply([&fn, id](auto*... value) { fn(id, *value...); }, values);
        }
    }

    /// \brief Number of entities that own every one of `Ts`.
    template <typename... Ts>
    [[nodiscard]] std::size_t countWith() const noexcept {
        static_assert(sizeof...(Ts) > 0, "countWith() needs at least one component type");
        const std::tuple<const detail::Storage<Ts>*...> storages{findStorage<Ts>()...};
        bool missing = false;
        std::apply([&missing](auto*... found) { ((missing = missing || (found == nullptr)), ...); }, storages);
        if (missing) {
            return 0;
        }
        std::size_t total = 0;
        for (std::size_t index = 1; index < m_entities.size(); ++index) {
            if (!m_entities[index].alive) {
                continue;
            }
            const EntityId id{static_cast<std::uint32_t>(index), m_entities[index].generation};
            bool complete = true;
            std::apply([&complete, id](auto*... found) { ((complete = complete && found->set.has(id)), ...); },
                       storages);
            if (complete) {
                ++total;
            }
        }
        return total;
    }

    [[nodiscard]] std::size_t liveEntities() const noexcept { return m_liveCount; }

    /// \brief Entity slots handed out so far, including recycled and freed ones.
    [[nodiscard]] std::size_t entitySlots() const noexcept { return m_entities.empty() ? 0 : m_entities.size() - 1; }

    [[nodiscard]] std::size_t registeredComponentTypes() const noexcept { return m_storages.size(); }

    [[nodiscard]] WorldStats stats() const noexcept;

    /// \brief Drops every entity and component, keeping the world usable.
    void clear() noexcept;

private:
    template <typename T>
    [[nodiscard]] detail::Storage<T>* findStorage() noexcept {
        const auto found = m_storages.find(componentTypeIdOf<T>());
        return found != m_storages.end() ? static_cast<detail::Storage<T>*>(found->second.get()) : nullptr;
    }

    template <typename T>
    [[nodiscard]] const detail::Storage<T>* findStorage() const noexcept {
        const auto found = m_storages.find(componentTypeIdOf<T>());
        return found != m_storages.end() ? static_cast<const detail::Storage<T>*>(found->second.get()) : nullptr;
    }

    struct Slot {
        std::uint32_t generation = 0;
        bool alive = false;
    };

    /// Slot 0 is never handed out; it stands for the null handle.
    std::vector<Slot> m_entities{Slot{}};
    std::vector<std::uint32_t> m_freeIndices;
    std::unordered_map<ComponentTypeId, std::unique_ptr<detail::StorageBase>> m_storages;
    std::size_t m_liveCount = 0;
    std::uint64_t m_createdTotal = 0;
    std::uint64_t m_destroyedTotal = 0;
};

}  // namespace fa::scene
