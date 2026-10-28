#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "fa/core/Assert.hpp"
#include "fa/scene/EntityId.hpp"

namespace fa::scene {

/// \file
/// Dense component storage with an O(1) sparse index.
///
/// The point of the split is cache behaviour: components live back to back in
/// `m_dense`, so iterating a component type touches one contiguous array no matter how
/// scattered the entity slots are. The sparse row maps an entity index to a position
/// in `m_dense`, and stores `position + 1` so that 0 can mean "absent" without a
/// separate bitmap.
///
/// Erasing swaps the last element into the hole. That keeps the array dense but means
/// a reference to a component is invalidated by any erase - reach for id-based access
/// rather than holding a pointer across a structural change.
template <typename T>
class SparseSet {
public:
    /// \brief Inserts `value`, or replaces the stored one when the entity already has it.
    /// \return Reference to the stored component, valid until the next erase.
    T& add(EntityId id, T value) {
        FA_ASSERT(id.index != 0, "entity index 0 is reserved for the null handle");
        const std::size_t slot = ensureSparseSlot(id.index);
        if (m_sparse[slot] != 0) {
            m_dense[m_sparse[slot] - 1].value = std::move(value);
            return m_dense[m_sparse[slot] - 1].value;
        }
        m_dense.push_back(Entry{id, std::move(value)});
        m_sparse[slot] = static_cast<std::uint32_t>(m_dense.size());
        return m_dense.back().value;
    }

    /// \brief Pointer to the component, or `nullptr` when `id` has none or is stale.
    T* tryGet(EntityId id) noexcept {
        const std::size_t slot = sparseSlot(id);
        if (slot == kNpos) {
            return nullptr;
        }
        return &m_dense[m_sparse[slot] - 1].value;
    }

    /// \copydoc tryGet(EntityId)
    const T* tryGet(EntityId id) const noexcept {
        const std::size_t slot = sparseSlot(id);
        if (slot == kNpos) {
            return nullptr;
        }
        return &m_dense[m_sparse[slot] - 1].value;
    }

    /// \brief Whether `id` currently owns a component here.
    bool has(EntityId id) const noexcept { return sparseSlot(id) != kNpos; }

    /// \brief Erases the component of `id`. \return false when there was nothing to erase.
    bool remove(EntityId id) noexcept {
        const std::size_t slot = sparseSlot(id);
        if (slot == kNpos) {
            return false;
        }
        const std::size_t position = m_sparse[slot] - 1;
        const std::size_t last = m_dense.size() - 1;
        if (position != last) {
            m_dense[position] = std::move(m_dense[last]);
            m_sparse[ensureSparseSlot(m_dense[position].entity.index)] = static_cast<std::uint32_t>(position + 1);
        }
        m_dense.pop_back();
        m_sparse[slot] = 0;
        return true;
    }

    std::size_t size() const noexcept { return m_dense.size(); }
    bool empty() const noexcept { return m_dense.empty(); }

    /// \brief Number of entity slots the sparse row can address, including dead ones.
    std::size_t sparseCapacity() const noexcept { return m_sparse.size(); }

    void clear() noexcept {
        m_dense.clear();
        m_sparse.clear();
    }

    /// \brief Entity stored at `denseIndex`, for iteration and tests.
    EntityId entityAt(std::size_t denseIndex) const noexcept {
        FA_ASSERT(denseIndex < m_dense.size(), "dense index out of range");
        return m_dense[denseIndex].entity;
    }

    /// \brief Component stored at `denseIndex`, for iteration and tests.
    T& valueAt(std::size_t denseIndex) noexcept {
        FA_ASSERT(denseIndex < m_dense.size(), "dense index out of range");
        return m_dense[denseIndex].value;
    }

    /// \copydoc valueAt(std::size_t)
    const T& valueAt(std::size_t denseIndex) const noexcept {
        FA_ASSERT(denseIndex < m_dense.size(), "dense index out of range");
        return m_dense[denseIndex].value;
    }

private:
    static constexpr std::size_t kNpos = static_cast<std::size_t>(-1);

    struct Entry {
        EntityId entity;
        T value;
    };

    /// \brief Maps an entity index to its dense position, or `kNpos` when absent.
    std::size_t sparseSlot(EntityId id) const noexcept {
        if (id.index == 0 || id.index >= m_sparse.size()) {
            return kNpos;
        }
        const std::uint32_t position = m_sparse[id.index];
        if (position == 0) {
            return kNpos;
        }
        if (m_dense[position - 1].entity != id) {
            return kNpos;
        }
        return id.index;
    }

    std::size_t ensureSparseSlot(std::uint32_t index) {
        if (index >= m_sparse.size()) {
            m_sparse.resize(static_cast<std::size_t>(index) + 1, 0);
        }
        return static_cast<std::size_t>(index);
    }

    std::vector<Entry> m_dense;
    std::vector<std::uint32_t> m_sparse;
};

}  // namespace fa::scene
