#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <fa/assets/AssetId.hpp>

namespace fa::assets {

/// \brief Lifecycle of one cached asset.
enum class AssetState : std::uint8_t {
    /// \brief Known to the cache but never requested.
    Unloaded,
    /// \brief A background job owns it; `get()` returns null until it is Ready.
    Loading,
    /// \brief Usable. A placeholder counts as Ready, see `TextureAsset::placeholder`.
    Ready,
    /// \brief Last attempt failed; `reload()` is the only way out.
    Failed,
    Count,
};

/// \brief Stable identifier name for logging and editor UI.
std::string_view toString(AssetState state) noexcept;

namespace detail {

/// \brief Type-erased cache slot.
///
/// The slot lives on the heap and never moves, so a handle stays valid for its whole
/// lifetime. Only `AssetManager` creates or destroys slots; handles just adjust the
/// reference count through this interface, which is why the concrete type stays out
/// of the header.
class AssetSlotBase {
public:
    AssetSlotBase() = default;
    virtual ~AssetSlotBase() = default;
    AssetSlotBase(const AssetSlotBase&) = delete;
    AssetSlotBase& operator=(const AssetSlotBase&) = delete;

    virtual void addRef() noexcept = 0;
    virtual void release() noexcept = 0;
    virtual int refCount() const noexcept = 0;
    virtual AssetState state() const noexcept = 0;
    virtual const AssetId& id() const noexcept = 0;

    /// \brief Pointer to the stored object, reinterpreted by the handle's `T`.
    /// Only meaningful while the state is Ready.
    virtual void* objectRaw() noexcept = 0;
};

}  // namespace detail

/// \brief Reference-counted, copyable access to one cached asset.
///
/// Copying a handle increments the slot's count; the last one to die lets the manager
/// evict the slot. Handles never carry an owning pointer, so a handle outliving its
/// manager is a use-after-free - drop every handle before destroying the manager
/// (`IAssetManager::releaseAll()` does it in one call).
///
/// A handle to a slot that is Loading or Failed is valid but empty: `get()` returns
/// null and `operator bool` is false, so gameplay can poll without a second map.
template <typename T>
class AssetHandle {
public:
    AssetHandle() = default;
    ~AssetHandle() { reset(); }

    AssetHandle(const AssetHandle& other) noexcept : m_slot(other.m_slot) {
        if (m_slot != nullptr) {
            m_slot->addRef();
        }
    }

    AssetHandle& operator=(const AssetHandle& other) noexcept {
        if (this != &other) {
            AssetHandle copy(other);
            swap(copy);
        }
        return *this;
    }

    AssetHandle(AssetHandle&& other) noexcept : m_slot(other.m_slot) { other.m_slot = nullptr; }

    AssetHandle& operator=(AssetHandle&& other) noexcept {
        if (this != &other) {
            reset();
            m_slot = other.m_slot;
            other.m_slot = nullptr;
        }
        return *this;
    }

    /// \brief Drops this reference; the slot may then be evicted.
    void reset() noexcept {
        if (m_slot != nullptr) {
            m_slot->release();
            m_slot = nullptr;
        }
    }

    void swap(AssetHandle& other) noexcept {
        detail::AssetSlotBase* const slot = m_slot;
        m_slot = other.m_slot;
        other.m_slot = slot;
    }

    /// \brief The asset, or null while loading and after a failure.
    T* get() const noexcept {
        if (m_slot == nullptr || m_slot->state() != AssetState::Ready) {
            return nullptr;
        }
        return static_cast<T*>(m_slot->objectRaw());
    }

    T* operator->() const noexcept { return get(); }

    /// \brief True only when the asset is usable right now.
    explicit operator bool() const noexcept { return get() != nullptr; }

    /// \brief Whether a slot is attached, regardless of its state.
    bool isValid() const noexcept { return m_slot != nullptr; }

    AssetState state() const noexcept { return m_slot != nullptr ? m_slot->state() : AssetState::Unloaded; }

    /// \brief Id of the attached slot; invalid for an empty handle.
    AssetId id() const noexcept { return m_slot != nullptr ? m_slot->id() : AssetId{}; }

    /// \brief Handles currently sharing this slot, including this one.
    int useCount() const noexcept { return m_slot != nullptr ? m_slot->refCount() : 0; }

private:
    friend class AssetManager;

    /// \brief Adopts \p slot and takes the reference the caller already owns.
    explicit AssetHandle(detail::AssetSlotBase* slot) noexcept : m_slot(slot) {}

    detail::AssetSlotBase* m_slot = nullptr;
};

}  // namespace fa::assets
