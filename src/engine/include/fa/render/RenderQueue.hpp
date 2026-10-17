#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "RenderTypes.hpp"

namespace fa::render {

/// \brief Collects this frame's draw items and hands them to the batching backend in order.
///
/// Submission order carries no meaning: the queue sorts by RenderLayer, then by
/// quantised sortY, then by entityId, and keeps insertion order as the final tie
/// break. That is what makes two actors standing on exactly the same row draw in
/// a stable order instead of flickering.
class RenderQueue {
public:
    /// \brief Drops every item; called by the renderer at the start of a frame.
    void clear() noexcept;

    /// \brief Appends \p item.
    ///
    /// Reserve capacity up front if a frame is expected to be large; growth is
    /// allowed and never reorders.
    void submit(const DrawItem& item);

    /// \brief Number of submitted items, sorted or not.
    std::size_t size() const noexcept { return m_items.size(); }

    bool empty() const noexcept { return m_items.empty(); }

    /// \brief Makes room for \p count items.
    void reserve(std::size_t count) { m_items.reserve(count); }

    /// \brief Sorts in place. Stable, so equal keys keep submission order.
    void sort();

    /// \brief The items, in draw order once sort() has run.
    std::span<const DrawItem> items() const noexcept { return m_items; }

    /// \brief Number of draw calls \p items needs with a single texture unit:
    /// one per run of consecutive items sharing a texture.
    ///
    /// Exposed so the cost of a queue can be inspected without a GL context.
    static std::size_t countDrawCalls(std::span<const DrawItem> items) noexcept;

    /// \brief How many times \p items switches texture, i.e. `countDrawCalls - 1`
    /// for a non-empty queue.
    static std::size_t countTextureSwitches(std::span<const DrawItem> items) noexcept;

private:
    std::vector<DrawItem> m_items;
};

}  // namespace fa::render
