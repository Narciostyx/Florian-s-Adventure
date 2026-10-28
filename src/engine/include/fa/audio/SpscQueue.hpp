#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <utility>

namespace fa::audio {

/// \brief Bounded lock-free queue for one producer and one consumer.
///
/// The audio thread is the only consumer; the main thread is the only producer.
/// `T` may be move-only. One slot stays empty so full and empty are
/// distinguishable without a separate counter. A full push is counted in
/// `dropped()` and returns false.
template <typename T, std::size_t Capacity>
class SpscQueue {
    static_assert(Capacity >= 2, "an SPSC ring needs at least two slots");

public:
    SpscQueue() = default;
    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;
    SpscQueue(SpscQueue&&) = delete;
    SpscQueue& operator=(SpscQueue&&) = delete;

    /// \brief Pushes one value; only the producer thread may call this.
    [[nodiscard]] bool tryPush(T value) {
        const std::size_t tail = m_tail.load(std::memory_order_relaxed);
        const std::size_t next = increment(tail);
        if (next == m_head.load(std::memory_order_acquire)) {
            m_dropped.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        m_slots[tail].emplace(std::move(value));
        m_tail.store(next, std::memory_order_release);
        return true;
    }

    /// \brief Pops one value; only the consumer thread may call this.
    [[nodiscard]] bool tryPop(T& out) {
        const std::size_t head = m_head.load(std::memory_order_relaxed);
        if (head == m_tail.load(std::memory_order_acquire)) {
            return false;
        }
        out = std::move(*m_slots[head]);
        m_slots[head].reset();
        m_head.store(increment(head), std::memory_order_release);
        return true;
    }

    /// \brief Approximate count; safe to call from either thread but not exact
    /// while the other side is active.
    [[nodiscard]] std::size_t size() const noexcept {
        const std::size_t head = m_head.load(std::memory_order_acquire);
        const std::size_t tail = m_tail.load(std::memory_order_acquire);
        return tail >= head ? tail - head : Capacity - head + tail;
    }

    [[nodiscard]] bool empty() const noexcept {
        return m_head.load(std::memory_order_acquire) == m_tail.load(std::memory_order_acquire);
    }

    /// \brief Number of pushes rejected because the queue was full.
    [[nodiscard]] std::size_t dropped() const noexcept { return m_dropped.load(std::memory_order_relaxed); }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity - 1; }

private:
    [[nodiscard]] static constexpr std::size_t increment(std::size_t index) noexcept { return (index + 1u) % Capacity; }

    std::array<std::optional<T>, Capacity> m_slots{};
    std::atomic<std::size_t> m_head{0};
    std::atomic<std::size_t> m_tail{0};
    std::atomic<std::size_t> m_dropped{0};
};

}  // namespace fa::audio
