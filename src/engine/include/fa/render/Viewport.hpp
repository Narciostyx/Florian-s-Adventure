#pragma once

#include <fa/core/Math.hpp>

#include "RenderConstants.hpp"

namespace fa::render {

/// \brief Integer-scaled, letterboxed mapping from the virtual surface to the window.
///
/// The only place that converts between virtual pixels and physical window
/// pixels. Nearest sampling plus an integer scale is what keeps a 640x360
/// surface sharp at any window size (docs/architecture/pixel-metrics.md); the
/// leftover pixels become black bars instead of being stretched.
struct Viewport {
    int windowWidth = 0;
    int windowHeight = 0;
    int virtualWidth = kVirtualWidth;
    int virtualHeight = kVirtualHeight;
    /// Whole-number magnification, never 0: a window smaller than the virtual
    /// surface gets scale 1 and is cropped rather than shrunk below 1:1.
    int scale = 1;
    /// Left edge of the drawn surface inside the window, in window pixels.
    int offsetX = 0;
    /// Top edge of the drawn surface inside the window, in window pixels.
    int offsetY = 0;

    /// \brief Computes the best fit for a window of \p windowWidth x \p windowHeight.
    static Viewport fit(int windowWidth,
                        int windowHeight,
                        int virtualWidth = kVirtualWidth,
                        int virtualHeight = kVirtualHeight) noexcept;

    /// \brief Width of the drawn surface in window pixels (`scale * virtualWidth`).
    int drawableWidth() const noexcept;

    /// \brief Height of the drawn surface in window pixels (`scale * virtualHeight`).
    int drawableHeight() const noexcept;

    /// \brief Whether the computed fit describes anything drawable.
    bool isEmpty() const noexcept;

    /// \brief The drawn surface as a rectangle in window pixels; the letterbox
    /// bars are everything outside it.
    core::Rect windowRect() const noexcept;

    /// \brief Window pixels to virtual pixels. No clamping: callers that care
    /// about the bars test against windowRect() first.
    core::Vec2 windowToVirtual(core::Vec2 windowPoint) const noexcept;

    /// \brief Virtual pixels to window pixels.
    core::Vec2 virtualToWindow(core::Vec2 virtualPoint) const noexcept;

    friend bool operator==(const Viewport& lhs, const Viewport& rhs) noexcept {
        return lhs.windowWidth == rhs.windowWidth && lhs.windowHeight == rhs.windowHeight &&
               lhs.virtualWidth == rhs.virtualWidth && lhs.virtualHeight == rhs.virtualHeight &&
               lhs.scale == rhs.scale && lhs.offsetX == rhs.offsetX && lhs.offsetY == rhs.offsetY;
    }
};

}  // namespace fa::render
