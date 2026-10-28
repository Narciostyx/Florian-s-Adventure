#include <fa/input/InputDevice.hpp>

#include <string_view>

namespace fa::input {

bool KeyboardDevice::handles(BindingKind bindingKind) const noexcept {
    return bindingKind == BindingKind::Key;
}

void KeyboardDevice::poll(std::span<const fa::platform::PlatformEvent> events) {
    for (const auto& event : events) {
        switch (event.type) {
            case fa::platform::PlatformEventType::FocusLost:
                reset();
                break;
            case fa::platform::PlatformEventType::KeyDown:
                if (event.scancode == fa::platform::Scancode::Unknown) {
                    break;
                }
                /// `repeat` is deliberately ignored: a held key is already reported by
                /// held(), and treating auto-repeat as a new press would make edge
                /// triggered actions fire at the OS repeat rate.
                if (m_down.insert(event.scancode).second) {
                    m_pressed.insert(event.scancode);
                }
                break;
            case fa::platform::PlatformEventType::KeyUp:
                if (event.scancode == fa::platform::Scancode::Unknown) {
                    break;
                }
                if (m_down.erase(event.scancode) > 0) {
                    m_released.insert(event.scancode);
                }
                break;
            case fa::platform::PlatformEventType::TextInput:
                m_text += event.text;
                break;
            default:
                break;
        }
    }
    if (m_text.size() > 256) {
        m_text.resize(256);
    }
}

void KeyboardDevice::endFrame() noexcept {
    m_pressed.clear();
    m_released.clear();
    m_text.clear();
}

void KeyboardDevice::reset() noexcept {
    m_down.clear();
    m_pressed.clear();
    m_released.clear();
    m_text.clear();
}

bool KeyboardDevice::held(fa::platform::Scancode scancode) const noexcept {
    return m_down.find(scancode) != m_down.end();
}

bool KeyboardDevice::pressed(fa::platform::Scancode scancode) const noexcept {
    return m_pressed.find(scancode) != m_pressed.end();
}

bool KeyboardDevice::released(fa::platform::Scancode scancode) const noexcept {
    return m_released.find(scancode) != m_released.end();
}

bool KeyboardDevice::held(const Binding& binding) const noexcept {
    return binding.kind == BindingKind::Key && held(binding.scancode);
}

bool KeyboardDevice::pressed(const Binding& binding) const noexcept {
    return binding.kind == BindingKind::Key && pressed(binding.scancode);
}

bool KeyboardDevice::released(const Binding& binding) const noexcept {
    return binding.kind == BindingKind::Key && released(binding.scancode);
}

float KeyboardDevice::value(const Binding& binding) const noexcept {
    return held(binding) ? binding.scale : 0.0f;
}

bool MouseDevice::handles(BindingKind bindingKind) const noexcept {
    /// MouseWheel bindings stay inert in v1: a notch is a delta, not a held state, so
    /// it is exposed through InputSystem::scroll() instead of ambiguous OR-ed actions.
    return bindingKind == BindingKind::MouseButton;
}

void MouseDevice::poll(std::span<const fa::platform::PlatformEvent> events) {
    for (const auto& event : events) {
        switch (event.type) {
            case fa::platform::PlatformEventType::FocusLost:
                reset();
                break;
            case fa::platform::PlatformEventType::MouseDown:
                if (event.button == fa::platform::MouseButton::None) {
                    break;
                }
                if (m_down.insert(event.button).second) {
                    m_pressed.insert(event.button);
                }
                break;
            case fa::platform::PlatformEventType::MouseUp:
                if (event.button == fa::platform::MouseButton::None) {
                    break;
                }
                if (m_down.erase(event.button) > 0) {
                    m_released.insert(event.button);
                }
                break;
            case fa::platform::PlatformEventType::MouseMove:
                m_position = fa::core::Vec2{event.x, event.y};
                m_delta += fa::core::Vec2{event.dx, event.dy};
                break;
            case fa::platform::PlatformEventType::MouseWheel:
                m_scroll += fa::core::Vec2{event.x, event.y};
                break;
            default:
                break;
        }
    }
}

void MouseDevice::endFrame() noexcept {
    m_pressed.clear();
    m_released.clear();
    m_delta = fa::core::Vec2{0.0f, 0.0f};
    m_scroll = fa::core::Vec2{0.0f, 0.0f};
}

void MouseDevice::reset() noexcept {
    m_down.clear();
    m_pressed.clear();
    m_released.clear();
    m_delta = fa::core::Vec2{0.0f, 0.0f};
    m_scroll = fa::core::Vec2{0.0f, 0.0f};
}

bool MouseDevice::held(fa::platform::MouseButton button) const noexcept {
    return m_down.find(button) != m_down.end();
}

bool MouseDevice::pressed(fa::platform::MouseButton button) const noexcept {
    return m_pressed.find(button) != m_pressed.end();
}

bool MouseDevice::released(fa::platform::MouseButton button) const noexcept {
    return m_released.find(button) != m_released.end();
}

bool MouseDevice::held(const Binding& binding) const noexcept {
    return binding.kind == BindingKind::MouseButton && held(binding.mouseButton);
}

bool MouseDevice::pressed(const Binding& binding) const noexcept {
    return binding.kind == BindingKind::MouseButton && pressed(binding.mouseButton);
}

bool MouseDevice::released(const Binding& binding) const noexcept {
    return binding.kind == BindingKind::MouseButton && released(binding.mouseButton);
}

float MouseDevice::value(const Binding& binding) const noexcept {
    return held(binding) ? binding.scale : 0.0f;
}

}  // namespace fa::input
