#include <fa/render/Viewport.hpp>

#include <algorithm>
#include <cmath>

namespace fa::render {

Viewport Viewport::fit(int windowWidth, int windowHeight, int virtualWidth, int virtualHeight, bool fill) noexcept {
    Viewport viewport;
    viewport.windowWidth = windowWidth;
    viewport.windowHeight = windowHeight;
    viewport.virtualWidth = virtualWidth > 0 ? virtualWidth : kVirtualWidth;
    viewport.virtualHeight = virtualHeight > 0 ? virtualHeight : kVirtualHeight;

    if (windowWidth <= 0 || windowHeight <= 0) {
        return viewport;
    }

    const float fitX = static_cast<float>(windowWidth) / static_cast<float>(viewport.virtualWidth);
    const float fitY = static_cast<float>(windowHeight) / static_cast<float>(viewport.virtualHeight);
    if (fill) {
        viewport.scale = std::max(0.1f, std::min(fitX, fitY));
    } else {
        const int scaleX = windowWidth / viewport.virtualWidth;
        const int scaleY = windowHeight / viewport.virtualHeight;
        viewport.scale = static_cast<float>(std::max(1, std::min(scaleX, scaleY)));
    }

    viewport.offsetX = (windowWidth - viewport.drawableWidth()) / 2;
    viewport.offsetY = (windowHeight - viewport.drawableHeight()) / 2;
    return viewport;
}

int Viewport::drawableWidth() const noexcept {
    return static_cast<int>(std::lround(static_cast<float>(virtualWidth) * scale));
}

int Viewport::drawableHeight() const noexcept {
    return static_cast<int>(std::lround(static_cast<float>(virtualHeight) * scale));
}

bool Viewport::isEmpty() const noexcept {
    return windowWidth <= 0 || windowHeight <= 0;
}

core::Rect Viewport::windowRect() const noexcept {
    return core::makeRect(static_cast<float>(offsetX),
                          static_cast<float>(offsetY),
                          static_cast<float>(drawableWidth()),
                          static_cast<float>(drawableHeight()));
}

core::Vec2 Viewport::windowToVirtual(core::Vec2 windowPoint) const noexcept {
    const float scaleF = static_cast<float>(scale);
    return core::Vec2{(windowPoint.x - static_cast<float>(offsetX)) / scaleF,
                      (windowPoint.y - static_cast<float>(offsetY)) / scaleF};
}

core::Vec2 Viewport::virtualToWindow(core::Vec2 virtualPoint) const noexcept {
    const float scaleF = static_cast<float>(scale);
    return core::Vec2{virtualPoint.x * scaleF + static_cast<float>(offsetX),
                      virtualPoint.y * scaleF + static_cast<float>(offsetY)};
}

}  // namespace fa::render
