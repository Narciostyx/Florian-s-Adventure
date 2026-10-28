#include <fa/platform/PlatformEvent.hpp>

namespace fa::platform {

std::string_view toString(PlatformEventType type) noexcept {
    switch (type) {
        case PlatformEventType::Quit:
            return "Quit";
        case PlatformEventType::WindowResized:
            return "WindowResized";
        case PlatformEventType::FocusGained:
            return "FocusGained";
        case PlatformEventType::FocusLost:
            return "FocusLost";
        case PlatformEventType::KeyDown:
            return "KeyDown";
        case PlatformEventType::KeyUp:
            return "KeyUp";
        case PlatformEventType::MouseDown:
            return "MouseDown";
        case PlatformEventType::MouseUp:
            return "MouseUp";
        case PlatformEventType::MouseMove:
            return "MouseMove";
        case PlatformEventType::MouseWheel:
            return "MouseWheel";
        case PlatformEventType::TextInput:
            return "TextInput";
        case PlatformEventType::FileDrop:
            return "FileDrop";
    }
    return "Unknown";
}

std::string_view toString(MouseButton button) noexcept {
    switch (button) {
        case MouseButton::Left:
            return "Left";
        case MouseButton::Middle:
            return "Middle";
        case MouseButton::Right:
            return "Right";
        case MouseButton::X1:
            return "X1";
        case MouseButton::X2:
            return "X2";
        case MouseButton::None:
            break;
    }
    return "None";
}

MouseButton mouseButtonFromName(std::string_view name) noexcept {
    for (const auto candidate :
         {MouseButton::Left, MouseButton::Middle, MouseButton::Right, MouseButton::X1, MouseButton::X2}) {
        if (toString(candidate) == name) {
            return candidate;
        }
    }
    return MouseButton::None;
}

}  // namespace fa::platform
