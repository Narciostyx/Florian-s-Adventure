#pragma once

#include <fa/core/Math.hpp>

#include "RenderConstants.hpp"

namespace fa::render {

/// \brief Follow tuning for Camera2D::follow().
///
/// The deadzone is a half-extent window around the camera centre: the target can
/// move that far in each direction without moving the view. Once it leaves the
/// window the camera eases toward it, optionally capped by `maxSpeed`.
struct CameraFollowConfig {
    core::Vec2 deadzone{0.0f, 0.0f};
    /// \brief Exponential easing rate in 1/s; 0 snaps to the target immediately.
    float smoothing = 8.0f;
    /// \brief Maximum follow speed in px/s; 0 or less means unlimited.
    float maxSpeed = 0.0f;
};

/// \brief Axis-aligned 2D camera: translation only, no zoom.
///
/// Position is the world-space point shown at the centre of the virtual surface.
/// Free zoom is deliberately absent: a pixel-art camera may only translate and
/// let the viewport apply a whole-number scale, otherwise sprite edges land on
/// fractional pixels and the art turns to mush.
///
/// Nothing here touches OpenGL; it only converts coordinates and builds the
/// projection matrix the renderer uploads.
class Camera2D {
public:
    /// \brief Centre of the view in world pixels.
    core::Vec2 position() const noexcept { return m_position; }

    /// \brief Moves the view, clamped to the configured bounds if any.
    void setPosition(core::Vec2 position) noexcept;

    /// \brief Moves the view by \p delta, clamped like setPosition().
    void move(core::Vec2 delta) noexcept;

    /// \brief Size of the virtual surface in pixels; defaults to 640x360.
    core::Vec2 viewportSize() const noexcept { return m_viewportSize; }

    /// \brief Sets the virtual surface size, e.g. for a different internal
    /// resolution while debugging. Values below 1 are ignored.
    void setViewportSize(core::Vec2 size) noexcept;

    /// \brief Region the camera may show, normally the map bounds.
    ///
    /// When set, the camera centre stays far enough inside it that no space
    /// outside the region becomes visible (unless the region is smaller than the
    /// viewport, in which case it is centred).
    void setBounds(core::Rect bounds) noexcept;

    /// \brief Replacement follow tuning; negative values are clamped to zero.
    void setFollowConfig(const CameraFollowConfig& config) noexcept;
    [[nodiscard]] const CameraFollowConfig& followConfig() const noexcept { return m_follow; }

    /// \brief Eases the camera toward \p target for one frame.
    ///
    /// The target is the world-space point that should end up near the centre.
    /// With a deadzone the camera only moves when the target leaves it; with
    /// smoothing 0 the camera snaps to the deadzone edge. Bounds are applied to
    /// the result, so following never reveals space outside the map.
    void follow(core::Vec2 target, float deltaSeconds) noexcept;

    /// \brief Removes the clamp set by setBounds().
    void clearBounds() noexcept;

    core::Rect bounds() const noexcept { return m_bounds; }
    bool hasBounds() const noexcept { return m_hasBounds; }

    /// \brief The world-space rectangle currently visible, in world pixels.
    core::Rect visibleRect() const noexcept;

    /// \brief World point to virtual pixel, without pixel snapping.
    ///
    /// The top-left of the visible rect maps to (0, 0) and the centre maps to
    /// half the viewport size.
    core::Vec2 worldToViewport(core::Vec2 worldPoint) const noexcept;

    /// \brief Virtual pixel to world point; the inverse of worldToViewport().
    core::Vec2 viewportToWorld(core::Vec2 viewportPoint) const noexcept;

    /// \brief World point to virtual pixel, rounded to whole pixels.
    ///
    /// Used by the renderer when building vertices. Rounding happens here and
    /// never in simulation state, so sub-pixel movement speeds still work.
    core::Vec2 worldToPixel(core::Vec2 worldPoint) const noexcept;

    /// \brief Snapped copy of this camera, aligned so that every world pixel
    /// lands on one virtual pixel.
    Camera2D snapped() const noexcept;

    /// \brief Writes the world-to-clip matrix as 16 floats in column-major order,
    /// ready for `glUniformMatrix4fv(..., GL_FALSE, ...)`.
    ///
    /// Y grows downward in world space, so the matrix flips it to match the
    /// clip-space convention where +y points up.
    void projection(float outColumnMajor[16]) const noexcept;

private:
    void applyBounds() noexcept;

    core::Vec2 m_position{};
    core::Vec2 m_viewportSize{static_cast<float>(kVirtualWidth), static_cast<float>(kVirtualHeight)};
    core::Rect m_bounds{};
    CameraFollowConfig m_follow{};
    bool m_hasBounds = false;
};

}  // namespace fa::render
