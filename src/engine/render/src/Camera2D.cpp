#include <fa/render/Camera2D.hpp>

#include <algorithm>
#include <cmath>

namespace fa::render {
namespace {

core::Vec2 halfSize(core::Vec2 size) noexcept {
    return core::Vec2{size.x * 0.5f, size.y * 0.5f};
}

}  // namespace

void Camera2D::setPosition(core::Vec2 position) noexcept {
    m_position = position;
    applyBounds();
}

void Camera2D::move(core::Vec2 delta) noexcept {
    setPosition(m_position + delta);
}

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

void Camera2D::setFollowConfig(const CameraFollowConfig& config) noexcept {
    m_follow.deadzone.x = std::max(0.0f, config.deadzone.x);
    m_follow.deadzone.y = std::max(0.0f, config.deadzone.y);
    m_follow.smoothing = std::max(0.0f, config.smoothing);
    m_follow.maxSpeed = std::max(0.0f, config.maxSpeed);
}

void Camera2D::follow(core::Vec2 target, float deltaSeconds) noexcept {
    if (!(deltaSeconds > 0.0f)) {
        return;
    }

    // The deadzone is a half-extent around the current centre. Inside it the
    // camera is already "close enough" and stays put; outside it the desired
    // centre is placed so the target sits exactly on the deadzone edge.
    core::Vec2 desired = m_position;
    const float dx = target.x - m_position.x;
    const float dy = target.y - m_position.y;
    if (dx > m_follow.deadzone.x) {
        desired.x = target.x - m_follow.deadzone.x;
    } else if (dx < -m_follow.deadzone.x) {
        desired.x = target.x + m_follow.deadzone.x;
    }
    if (dy > m_follow.deadzone.y) {
        desired.y = target.y - m_follow.deadzone.y;
    } else if (dy < -m_follow.deadzone.y) {
        desired.y = target.y + m_follow.deadzone.y;
    }

    const float deltaX = desired.x - m_position.x;
    const float deltaY = desired.y - m_position.y;
    const float distance = std::sqrt(deltaX * deltaX + deltaY * deltaY);
    if (distance <= 0.0f) {
        return;
    }

    // Exponential easing is frame-rate independent. Smoothing 0 means a hard
    // snap, which tests and cut-scenes can use.
    float step = distance;
    if (m_follow.smoothing > 0.0f) {
        const float alpha = 1.0f - std::exp(-m_follow.smoothing * deltaSeconds);
        step = distance * std::max(0.0f, alpha);
    }
    if (m_follow.maxSpeed > 0.0f) {
        step = std::min(step, m_follow.maxSpeed * deltaSeconds);
    }

    // Snap the last fraction of a pixel so bounds are reached exactly; otherwise
    // an exponential approach asymptotically stops just short of the clamp.
    if (step >= distance - 1.0e-4f) {
        setPosition(desired);
        return;
    }

    const float scale = step / distance;
    setPosition(core::Vec2{m_position.x + deltaX * scale, m_position.y + deltaY * scale});
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
