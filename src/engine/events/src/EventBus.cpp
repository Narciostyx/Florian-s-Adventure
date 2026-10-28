#include "fa/events/EventBus.hpp"

#include <string>
#include <utility>

#include <fa/core/Log.hpp>

namespace fa::events {

void EventBus::assertOwnerThread(const char* operation) const noexcept {
    if (std::this_thread::get_id() == m_ownerThread) {
        return;
    }
    core::logf(core::LogCategory::Events,
               core::LogLevel::Fatal,
               "EventBus: %s called from a thread that does not own the bus",
               operation);
    core::fatal("EventBus: subscribe, unsubscribe, publish and dispatchQueued are owner-thread only");
}

void EventBus::unsubscribe(SubscriptionId id) noexcept {
    if (id == kInvalidSubscriptionId) {
        return;
    }
    assertOwnerThread("EventBus::unsubscribe");
    for (auto& entry : m_buckets) {
        entry.second->remove(id);
    }
}

std::size_t EventBus::dispatchQueued() {
    assertOwnerThread("EventBus::dispatchQueued");

    std::size_t delivered = 0;
    std::vector<detail::QueuedEvent> batch;
    {
        std::scoped_lock lock(m_queueMutex);
        batch.swap(m_queue);
    }

    int waves = 0;
    while (!batch.empty()) {
        ++waves;
        if (waves > detail::kMaxDispatchWaves) {
            std::scoped_lock lock(m_queueMutex);
            for (detail::QueuedEvent& queued : batch) {
                m_queue.push_back(std::move(queued));
            }
            const std::size_t deferred = m_queue.size();
            core::logf(core::LogCategory::Events,
                       core::LogLevel::Warn,
                       "EventBus: dispatchQueued stopped after %d waves, %llu event(s) retried next frame",
                       detail::kMaxDispatchWaves,
                       static_cast<unsigned long long>(deferred));
            break;
        }

        std::vector<detail::QueuedEvent> current;
        current.swap(batch);
        for (detail::QueuedEvent& queued : current) {
            queued.deliver();
            ++delivered;
        }

        // Handlers may have enqueued more work; it is ordered behind this batch.
        std::scoped_lock lock(m_queueMutex);
        batch.swap(m_queue);
    }

    return delivered;
}

void EventBus::clear() {
    assertOwnerThread("EventBus::clear");
    {
        std::scoped_lock lock(m_queueMutex);
        m_queue.clear();
    }
    m_buckets.clear();
}

std::size_t EventBus::queuedCount() const noexcept {
    std::scoped_lock lock(m_queueMutex);
    return m_queue.size();
}

std::size_t EventBus::totalSubscriberCount() const noexcept {
    std::size_t total = 0;
    for (const auto& entry : m_buckets) {
        total += entry.second->subscriberCount();
    }
    return total;
}

}  // namespace fa::events
