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

}  // namespace fa::platform
