#include <fa/render/Camera2D.hpp>

namespace fa::render {
namespace {

core::Vec2 halfSize(core::Vec2 size) noexcept { return core::Vec2{size.x * 0.5f, size.y * 0.5f}; }

}  // namespace

void Camera2D::setPosition(core::Vec2 position) noexcept {
    m_position = position;
    applyBounds();
}

void Camera2D::move(core::Vec2 delta) noexcept { setPosition(m_position + delta); }

void Camera2D::setViewportSize(core::Vec2 size) noexcept {
    if (size.x < 1.0f || size.y < 1.0f) {
        return;
    }
    m_viewportSize = size;
    applyBounds();
}

void Camera2D::setBounds(core::Rect bounds) noexcept {
    m_bounds = bounds;
    m_hasBounds = !core::isEmpty(bounds);
    applyBounds();
}

void Camera2D::clearBounds() noexcept {
    m_hasBounds = false;
    m_bounds = core::Rect{};
}

void Camera2D::applyBounds() noexcept {
    if (!m_hasBounds) {
        return;
    }
    const core::Vec2 half = halfSize(m_viewportSize);

    if (m_bounds.w >= m_viewportSize.x) {
        m_position.x = core::clampf(m_position.x, m_bounds.x + half.x, core::right(m_bounds) - half.x);
    } else {
        m_position.x = core::center(m_bounds).x;
    }

    if (m_bounds.h >= m_viewportSize.y) {
        m_position.y = core::clampf(m_position.y, m_bounds.y + half.y, core::bottom(m_bounds) - half.y);
    } else {
        m_position.y = core::center(m_bounds).y;
    }
}

core::Rect Camera2D::visibleRect() const noexcept {
    const core::Vec2 half = halfSize(m_viewportSize);
    return core::makeRect(m_position.x - half.x, m_position.y - half.y, m_viewportSize.x, m_viewportSize.y);
}

core::Vec2 Camera2D::worldToViewport(core::Vec2 worldPoint) const noexcept {
    const core::Vec2 half = halfSize(m_viewportSize);
    return core::Vec2{worldPoint.x - m_position.x + half.x, worldPoint.y - m_position.y + half.y};
}

core::Vec2 Camera2D::viewportToWorld(core::Vec2 viewportPoint) const noexcept {
    const core::Vec2 half = halfSize(m_viewportSize);
    return core::Vec2{viewportPoint.x + m_position.x - half.x, viewportPoint.y + m_position.y - half.y};
}

core::Vec2 Camera2D::worldToPixel(core::Vec2 worldPoint) const noexcept {
    const core::Vec2 viewportPoint = worldToViewport(worldPoint);
    return core::Vec2{core::snapToPixel(viewportPoint.x), core::snapToPixel(viewportPoint.y)};
}

Camera2D Camera2D::snapped() const noexcept {
    Camera2D copy = *this;
    copy.m_position = core::Vec2{core::snapToPixel(m_position.x), core::snapToPixel(m_position.y)};
    return copy;
}

void Camera2D::projection(float outColumnMajor[16]) const noexcept {
    const core::Rect visible = visibleRect();
    const float width = visible.w;
    const float height = visible.h;

    // Column-major, matching what glUniformMatrix4fv wants with transpose = false.
    outColumnMajor[0] = 2.0f / width;
    outColumnMajor[1] = 0.0f;
    outColumnMajor[2] = 0.0f;
    outColumnMajor[3] = 0.0f;

    outColumnMajor[4] = 0.0f;
    outColumnMajor[5] = -2.0f / height;
    outColumnMajor[6] = 0.0f;
    outColumnMajor[7] = 0.0f;

    outColumnMajor[8] = 0.0f;
    outColumnMajor[9] = 0.0f;
    outColumnMajor[10] = -1.0f;
    outColumnMajor[11] = 0.0f;

    outColumnMajor[12] = -1.0f - 2.0f * visible.x / width;
    outColumnMajor[13] = 1.0f + 2.0f * visible.y / height;
    outColumnMajor[14] = 0.0f;
    outColumnMajor[15] = 1.0f;
}

}  // namespace fa::render
