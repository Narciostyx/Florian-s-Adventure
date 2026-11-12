#include "fa/ui/Ui.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace fa::ui {

namespace {

[[nodiscard]] constexpr std::uint32_t fnv1a(std::string_view name) noexcept {
    std::uint32_t hash = 2166136261u;
    for (const char character : name) {
        hash ^= static_cast<std::uint8_t>(character);
        hash *= 16777619u;
    }
    return hash;
}

[[nodiscard]] TextStyle widgetTextStyle(const UiTheme& theme, TextAlign align) noexcept {
    TextStyle style;
    style.color = theme.text;
    style.scale = theme.textScale;
    style.align = align;
    return style;
}

}  // namespace

UiId UiId::fromName(std::string_view name) noexcept {
    const std::uint32_t hash = fnv1a(name);
    return UiId{hash == 0u ? 1u : hash};
}

UiContext::UiContext(const FontAtlas& font, render::TextureHandle whiteTexture, UiTheme theme)
    : m_font(font), m_white(whiteTexture), m_theme(theme) {}

void UiContext::beginFrame(const UiInputState& input) noexcept {
    m_queue.clear();
    m_input = input;
    // A release event ends a drag; clearing here also recovers when the window
    // loses the mouse-up (focus loss) and no release ever arrives.
    if (m_active != 0u && !m_input.mouseDown && !m_input.mouseReleased) {
        m_active = 0u;
    }

    // Keyboard focus chain. Widgets recorded this frame populate `m_navCur`;
    // navigation walks `m_navPrev` (last frame's submitted buttons).
    m_navPrev.swap(m_navCur);
    m_navCur.clear();
    m_navActivate = false;
    if (m_focus != 0u && std::find(m_navPrev.begin(), m_navPrev.end(), m_focus) == m_navPrev.end()) {
        m_focus = 0u;
    }
    if (input.upPressed || input.downPressed) {
        m_navKeyboard = true;
        if (!m_navPrev.empty()) {
            if (m_focus == 0u) {
                m_focus = m_navPrev.front();
            } else {
                const auto found = std::find(m_navPrev.begin(), m_navPrev.end(), m_focus);
                std::size_t index = static_cast<std::size_t>(found - m_navPrev.begin());
                const std::size_t count = m_navPrev.size();
                if (input.downPressed) {
                    index = (index + 1u) % count;
                } else {
                    index = (index + count - 1u) % count;
                }
                m_focus = m_navPrev[index];
            }
        }
    }
    if (input.activatePressed && m_focus != 0u && m_navKeyboard) {
        m_navActivate = true;
    }
}

void UiContext::setFocus(UiId id) noexcept {
    m_focus = id.value;
    m_navKeyboard = id.value != 0u;
}

void UiContext::panel(core::Rect rect, core::Color color) {
    quad(offset(rect), color);
}

void UiContext::border(core::Rect rect, core::Color color, float thickness) {
    const float t = thickness > 0.0f ? thickness : 1.0f;
    quad(offset(core::makeRect(rect.x, rect.y, rect.w, t)), color);
    quad(offset(core::makeRect(rect.x, rect.y + rect.h - t, rect.w, t)), color);
    const float middleHeight = rect.h - 2.0f * t;
    if (middleHeight > 0.0f) {
        quad(offset(core::makeRect(rect.x, rect.y + t, t, middleHeight)), color);
        quad(offset(core::makeRect(rect.x + rect.w - t, rect.y + t, t, middleHeight)), color);
    }
}

void UiContext::progressBar(core::Rect rect, float value, float minimum, float maximum, core::Color fill) {
    quad(offset(rect), m_theme.track);
    if (!(maximum > minimum)) {
        return;
    }
    const float fraction = core::clamp01((value - minimum) / (maximum - minimum));
    if (fraction <= 0.0f) {
        return;
    }
    quad(offset(core::makeRect(rect.x, rect.y, rect.w * fraction, rect.h)), fill);
}

void UiContext::icon(core::Rect rect, render::TextureHandle texture, core::Color tint) {
    if (rect.w <= 0.0f || rect.h <= 0.0f || !texture.isValid()) {
        return;
    }
    const core::Rect placed = offset(rect);
    render::DrawItem item;
    item.texture = texture;
    item.position = core::Vec2{placed.x, placed.y};
    item.size = core::Vec2{placed.w, placed.h};
    item.tint = tint;
    item.layer = render::RenderLayer::UI;
    item.sortY = placed.y;
    item.entityId = m_nextEntityId++;
    m_queue.submit(item);
}

void UiContext::text(std::string_view value, core::Rect rect, const TextStyle& style) {
    ::fa::ui::drawText(m_font, value, offset(rect), style, m_queue);
}

UiResult UiContext::button(UiId id, std::string_view label, core::Rect rect, render::TextureHandle background) {
    UiResult result;
    result.hovered = hovered(rect);

    if (std::find(m_navCur.begin(), m_navCur.end(), id.value) == m_navCur.end()) {
        m_navCur.push_back(id.value);
    }

    if (m_input.mousePressed && result.hovered) {
        m_active = id.value;
        // A click owns the focus until the keyboard moves it again.
        m_focus = id.value;
        m_navKeyboard = false;
    }
    if (m_input.mouseReleased && m_active == id.value) {
        result.pressed = result.hovered;
        m_active = 0u;
    }
    if (m_navActivate && id.value == m_focus) {
        result.pressed = true;
    }

    const bool held = m_active == id.value;
    if (background.isValid()) {
        // Image button: hover brightens, press darkens; no theme quad or border.
        float level = 1.0f;
        if (held) {
            level = 0.72f;
        } else if (result.hovered) {
            level = 1.18f;
        }
        icon(rect, background, core::Color{level, level, level, 1.0f});
    } else {
        const core::Color fill = held ? m_theme.buttonPressed : (result.hovered ? m_theme.buttonHover : m_theme.button);
        quad(offset(rect), fill);
        border(rect, m_theme.panelBorder);
    }
    if (m_navKeyboard && id.value == m_focus) {
        border(core::makeRect(rect.x - 1.0f, rect.y - 1.0f, rect.w + 2.0f, rect.h + 2.0f), m_theme.accent, 1.0f);
    }

    const TextStyle style = widgetTextStyle(m_theme, TextAlign::Center);
    const float innerWidth = rect.w - 2.0f * m_theme.padding;
    const TextMetrics metrics = measureText(m_font, label, innerWidth, style);
    const core::Rect textRect{
        rect.x + m_theme.padding, rect.y + (rect.h - metrics.height) * 0.5f, innerWidth, metrics.height};
    text(label, textRect, style);
    return result;
}

UiResult UiContext::checkbox(UiId id, std::string_view label, bool& value, core::Rect rect) {
    UiResult result;
    result.hovered = hovered(rect);

    if (m_input.mousePressed && result.hovered) {
        m_active = id.value;
    }
    if (m_input.mouseReleased && m_active == id.value) {
        if (result.hovered) {
            value = !value;
            result.changed = true;
            result.pressed = true;
        }
        m_active = 0u;
    }

    const float boxSize = rect.h;
    const core::Rect box{rect.x, rect.y, boxSize, boxSize};
    quad(offset(box), result.hovered ? m_theme.buttonHover : m_theme.button);
    border(box, m_theme.panelBorder);
    if (value) {
        const float inset = boxSize * 0.25f;
        quad(offset(core::makeRect(box.x + inset, box.y + inset, boxSize - 2.0f * inset, boxSize - 2.0f * inset)),
             m_theme.accent);
    }

    const TextStyle style = widgetTextStyle(m_theme, TextAlign::Left);
    const float labelX = rect.x + boxSize + m_theme.padding;
    const float labelWidth = rect.w - boxSize - m_theme.padding;
    if (labelWidth > 0.0f) {
        const TextMetrics metrics = measureText(m_font, label, labelWidth, style);
        text(label,
             core::makeRect(labelX, rect.y + (rect.h - metrics.height) * 0.5f, labelWidth, metrics.height),
             style);
    }
    return result;
}

UiResult UiContext::slider(UiId id, float& value, float minimum, float maximum, core::Rect rect) {
    UiResult result;
    result.hovered = hovered(rect);

    if (m_input.mousePressed && result.hovered) {
        m_active = id.value;
    }

    const bool editable = maximum > minimum && rect.w > 0.0f;
    if (m_active == id.value && (m_input.mouseDown || m_input.mouseReleased) && editable) {
        const float fraction = core::clamp01((m_input.mousePosition.x - rect.x) / rect.w);
        const float next = minimum + (maximum - minimum) * fraction;
        if (next != value) {
            value = next;
            result.changed = true;
        }
    }
    if (m_input.mouseReleased && m_active == id.value) {
        m_active = 0u;
    }

    const float trackHeight = rect.h * 0.25f > 2.0f ? rect.h * 0.25f : 2.0f;
    const core::Rect track{rect.x, rect.y + (rect.h - trackHeight) * 0.5f, rect.w, trackHeight};
    quad(offset(track), m_theme.track);

    const float fraction = editable ? core::clamp01((value - minimum) / (maximum - minimum)) : 0.0f;
    if (fraction > 0.0f) {
        quad(offset(core::makeRect(track.x, track.y, track.w * fraction, track.h)), m_theme.accent);
    }

    const float knobSize = rect.h * 0.6f > 4.0f ? rect.h * 0.6f : 4.0f;
    const float knobX = rect.x + rect.w * fraction - knobSize * 0.5f;
    const core::Color knobColor = (result.hovered || m_active == id.value) ? m_theme.text : m_theme.buttonHover;
    quad(offset(core::makeRect(knobX, rect.y + (rect.h - knobSize) * 0.5f, knobSize, knobSize)), knobColor);
    return result;
}

void UiContext::submit(render::RenderQueue& out) const {
    for (const render::DrawItem& item : m_queue.items()) {
        out.submit(item);
    }
}

bool UiContext::hovered(core::Rect rect) const noexcept {
    return core::rectContains(rect, m_input.mousePosition);
}

core::Rect UiContext::offset(core::Rect rect) const noexcept {
    return core::makeRect(rect.x + m_origin.x, rect.y + m_origin.y, rect.w, rect.h);
}

void UiContext::quad(core::Rect rect, core::Color color) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) {
        return;
    }
    render::DrawItem item;
    item.texture = m_white;
    item.position = core::Vec2{rect.x, rect.y};
    item.size = core::Vec2{rect.w, rect.h};
    item.tint = color;
    item.layer = render::RenderLayer::UI;
    item.sortY = rect.y;
    item.entityId = m_nextEntityId++;
    m_queue.submit(item);
}

}  // namespace fa::ui
