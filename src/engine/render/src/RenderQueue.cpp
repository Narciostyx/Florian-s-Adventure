#include <fa/render/RenderQueue.hpp>

#include <algorithm>

namespace fa::render {

void RenderQueue::clear() noexcept {
    m_items.clear();
}

void RenderQueue::submit(const DrawItem& item) {
    m_items.push_back(item);
}

void RenderQueue::sort() {
    // Stable so that two items agreeing on layer, depth and entity keep their
    // submission order instead of swapping every frame.
    std::stable_sort(m_items.begin(), m_items.end(), drawsBefore);
}

std::size_t RenderQueue::countDrawCalls(std::span<const DrawItem> items) noexcept {
    std::size_t calls = 0;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i == 0 || items[i].texture != items[i - 1].texture) {
            ++calls;
        }
    }
    return calls;
}

std::size_t RenderQueue::countTextureSwitches(std::span<const DrawItem> items) noexcept {
    const std::size_t calls = countDrawCalls(items);
    return calls == 0 ? 0 : calls - 1;
}

}  // namespace fa::render
