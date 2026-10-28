#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fa/core/Assert.hpp>

namespace fa::events {

/// \brief Handle to one subscription. Never 0 for a live subscription, so a
/// zero-initialized value is always "not subscribed".
using SubscriptionId = std::uint64_t;

inline constexpr SubscriptionId kInvalidSubscriptionId = 0;

/// \brief Process-unique identifier of an event type.
///
/// RTTI is disabled solution-wide (`/GR-`), so `typeid` and `dynamic_cast` are not
/// available. Ids are handed out on first use of a type instead. They are an
/// in-memory key only: two runs of the same binary may assign different numbers,
/// so they must never be written to a save file or a network message.
using EventTypeId = std::uint32_t;

namespace detail {

/// \brief Allocates the next event type id. Thread-safe; never returns 0.
inline EventTypeId allocateEventTypeId() noexcept {
    static std::atomic<EventTypeId> counter{1};
    return counter.fetch_add(1, std::memory_order_relaxed);
}

/// \brief Compile-time name of an event type, for log lines and the debug overlay.
/// RTTI is off, so the name is scraped out of the compiler's function signature.
template <typename E>
constexpr std::string_view eventTypeNameView() noexcept {
    constexpr std::string_view signature = __FUNCSIG__;
    constexpr std::string_view marker = "eventTypeNameView<";
    const std::size_t start = signature.find(marker);
    const std::size_t end = signature.rfind(">(");
    if (start == std::string_view::npos || end == std::string_view::npos || end <= start) {
        return {};
    }
    std::string_view name = signature.substr(start + marker.size(), end - start - marker.size());
    for (const std::string_view prefix : {"struct ", "class ", "enum "}) {
        if (name.starts_with(prefix)) {
            name.remove_prefix(prefix.size());
            break;
        }
    }
    return name;
}

/// \brief Id of `E`, allocated once per process on first call.
template <typename E>
EventTypeId eventTypeIdOf() noexcept {
    static const EventTypeId id = allocateEventTypeId();
    return id;
}

class EventBucketBase {
public:
    EventBucketBase() = default;
    virtual ~EventBucketBase() = default;
    EventBucketBase(const EventBucketBase&) = delete;
    EventBucketBase& operator=(const EventBucketBase&) = delete;

    virtual void remove(SubscriptionId id) = 0;
    virtual void compact() = 0;
    virtual std::size_t subscriberCount() const noexcept = 0;
};

/// \brief Handlers for exactly one event type.
///
/// A separate class per type keeps `publish` a virtual-free call: the bus looks the
/// bucket up by type id and static-casts, which is sound because the id is derived
/// from the type itself and never handed out to another type.
template <typename E>
class EventBucket final : public EventBucketBase {
public:
    using Handler = std::function<void(const E&)>;

    void add(SubscriptionId id, Handler handler) { m_slots.push_back(Slot{id, true, std::move(handler)}); }

    void remove(SubscriptionId id) override {
        for (Slot& slot : m_slots) {
            if (slot.id == id) {
                slot.active = false;
                slot.handler = nullptr;
                return;
            }
        }
    }

    void compact() override {
        std::erase_if(m_slots, [](const Slot& slot) { return !slot.active; });
    }

    std::size_t subscriberCount() const noexcept override {
        std::size_t count = 0;
        for (const Slot& slot : m_slots) {
            if (slot.active) {
                ++count;
            }
        }
        return count;
    }

    /// \brief Invokes every handler that was subscribed when this call started.
    ///
    /// Handlers registered during the dispatch are skipped until the next publish.
    /// This snapshot rule is what makes subscribing from inside a handler safe.
    std::size_t dispatch(const E& event) {
        const std::size_t snapshot = m_slots.size();
        std::size_t invoked = 0;
        ++m_depth;
        for (std::size_t i = 0; i < snapshot; ++i) {
            if (!m_slots[i].active) {
                continue;
            }
            // Copied deliberately: a handler may subscribe or unsubscribe, which can
            // reallocate m_slots while this iteration is still on the stack.
            const Handler handler = m_slots[i].handler;
            if (!handler) {
                continue;
            }
            handler(event);
            ++invoked;
        }
        --m_depth;
        if (m_depth == 0) {
            compact();
        }
        return invoked;
    }

private:
    struct Slot {
        SubscriptionId id = kInvalidSubscriptionId;
        bool active = true;
        Handler handler;
    };

    std::vector<Slot> m_slots;
    int m_depth = 0;
};

struct QueuedEvent {
    EventTypeId typeId = 0;
    std::function<void()> deliver;
};

/// \brief Ceiling on delivery rounds inside one dispatchQueued() call.
///
/// A handler that enqueues another event every time would otherwise keep that
/// function looping forever. The remainder is pushed back and retried next frame.
inline constexpr int kMaxDispatchWaves = 8;

}  // namespace detail

/// \brief Compile-time name of an event type, for diagnostics only.
template <typename E>
std::string_view eventTypeName() noexcept {
    return detail::eventTypeNameView<E>();
}

/// \brief Typed, decoupled messaging between modules.
///
/// Threading rules (docs/modules/03-events-and-jobs.md):
///   - subscribe / unsubscribe / publish / dispatchQueued / clear: owning thread only.
///     Ownership is the thread that constructed the bus; violations abort in Debug.
///   - enqueue: any thread, including job workers. That is the one supported way for
///     background work to talk to the main thread.
///
/// Event payloads are copied when enqueued, so the caller's object may die right
/// after the call. Nothing is serialized: `E` only needs a copy constructor.
///
/// `E` should be a small value type. Handlers run synchronously on the publishing
/// thread; keep them short and never block on a job from inside a handler.
class EventBus final {
public:
    EventBus() = default;

    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    /// \brief Subscribes to `E`; the handler runs on every publish of `E`.
    /// \return A token usable with unsubscribe(); never kInvalidSubscriptionId.
    template <typename E>
    SubscriptionId subscribe(std::function<void(const E&)> handler) {
        assertOwnerThread("EventBus::subscribe");
        FA_ASSERT(handler != nullptr, "EventBus::subscribe requires a callable");
        const SubscriptionId id = allocateSubscriptionId();
        bucketFor<E>(true)->add(id, std::move(handler));
        return id;
    }

    /// \brief Drops a subscription. Unknown and already-removed ids are ignored.
    ///
    /// Safe to call from inside a handler: the slot is tombstoned immediately, so a
    /// later handler in the same dispatch is not invoked, and the memory is released
    /// once the outermost dispatch returns.
    void unsubscribe(SubscriptionId id) noexcept;

    /// \brief Delivers `event` synchronously to the current subscribers.
    /// \return How many handlers ran.
    template <typename E>
    std::size_t publish(const E& event) {
        assertOwnerThread("EventBus::publish");
        detail::EventBucket<E>* bucket = bucketFor<E>(false);
        return bucket == nullptr ? 0 : bucket->dispatch(event);
    }

    /// \brief Queues a copy of `event` for the next dispatchQueued() on the main thread.
    /// Thread-safe. This is what job workers use to reach the main thread.
    template <typename E>
    void enqueue(E event) {
        detail::QueuedEvent queued;
        queued.typeId = detail::eventTypeIdOf<E>();
        queued.deliver = [this, stored = std::move(event)]() { publish<E>(stored); };
        std::scoped_lock lock(m_queueMutex);
        m_queue.push_back(std::move(queued));
    }

    /// \brief Delivers everything enqueued so far, in enqueue order across all types.
    /// \return How many events were delivered.
    std::size_t dispatchQueued();

    /// \brief Drops queued events and every subscription. Never call mid-dispatch.
    void clear();

    /// \brief Number of events waiting for dispatchQueued().
    std::size_t queuedCount() const noexcept;

    /// \brief Live subscriptions for `E`.
    template <typename E>
    std::size_t subscriberCount() const noexcept {
        const auto entry = m_buckets.find(detail::eventTypeIdOf<E>());
        if (entry == m_buckets.end()) {
            return 0;
        }
        return entry->second->subscriberCount();
    }

    /// \brief Live subscriptions across every event type.
    std::size_t totalSubscriberCount() const noexcept;

private:
    template <typename E>
    detail::EventBucket<E>* bucketFor(bool create) {
        const EventTypeId id = detail::eventTypeIdOf<E>();
        const auto entry = m_buckets.find(id);
        if (entry != m_buckets.end()) {
            return static_cast<detail::EventBucket<E>*>(entry->second.get());
        }
        if (!create) {
            return nullptr;
        }
        auto bucket = std::make_unique<detail::EventBucket<E>>();
        detail::EventBucket<E>* raw = bucket.get();
        m_buckets.emplace(id, std::move(bucket));
        return raw;
    }

    SubscriptionId allocateSubscriptionId() noexcept { return ++m_nextSubscriptionId; }

    void assertOwnerThread(const char* operation) const noexcept;

    std::unordered_map<EventTypeId, std::unique_ptr<detail::EventBucketBase>> m_buckets;
    std::vector<detail::QueuedEvent> m_queue;
    mutable std::mutex m_queueMutex;
    std::thread::id m_ownerThread = std::this_thread::get_id();
    SubscriptionId m_nextSubscriptionId = kInvalidSubscriptionId;
};

/// \brief RAII subscription: unsubscribes when it leaves scope.
///
/// The token does not own the bus. It must be destroyed (or release()d) before the
/// EventBus it came from, which in practice means declaring it after the bus.
class Subscription final {
public:
    Subscription() = default;

    Subscription(EventBus& bus, SubscriptionId id) noexcept : m_bus(&bus), m_id(id) {}

    ~Subscription() { reset(); }

    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;

    Subscription(Subscription&& other) noexcept : m_bus(other.m_bus), m_id(other.m_id) {
        other.m_bus = nullptr;
        other.m_id = kInvalidSubscriptionId;
    }

    Subscription& operator=(Subscription&& other) noexcept {
        if (this != &other) {
            reset();
            m_bus = other.m_bus;
            m_id = other.m_id;
            other.m_bus = nullptr;
            other.m_id = kInvalidSubscriptionId;
        }
        return *this;
    }

    bool isValid() const noexcept { return m_bus != nullptr && m_id != kInvalidSubscriptionId; }

    SubscriptionId id() const noexcept { return m_id; }

    /// \brief Unsubscribes now. Safe to call repeatedly.
    void reset() noexcept {
        if (isValid()) {
            m_bus->unsubscribe(m_id);
        }
        m_bus = nullptr;
        m_id = kInvalidSubscriptionId;
    }

    /// \brief Gives up ownership without unsubscribing.
    SubscriptionId release() noexcept {
        const SubscriptionId id = m_id;
        m_bus = nullptr;
        m_id = kInvalidSubscriptionId;
        return id;
    }

private:
    EventBus* m_bus = nullptr;
    SubscriptionId m_id = kInvalidSubscriptionId;
};

}  // namespace fa::events
