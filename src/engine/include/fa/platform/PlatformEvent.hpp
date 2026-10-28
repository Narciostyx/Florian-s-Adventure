#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <fa/platform/Scancode.hpp>

namespace fa::platform {

/// \brief Kind of platform event delivered by IPlatform::pumpEvents().
enum class PlatformEventType : std::uint8_t {
    Quit,
    WindowResized,
    FocusGained,
    FocusLost,
    KeyDown,
    KeyUp,
    MouseDown,
    MouseUp,
    MouseMove,
    MouseWheel,
    TextInput,
    FileDrop,
};

/// \brief Mouse button identity; the numeric values match SDL's button indices.
enum class MouseButton : std::uint8_t {
    None = 0,
    Left = 1,
    Middle = 2,
    Right = 3,
    X1 = 4,
    X2 = 5,
};

/// \brief Backend-agnostic value type describing a single platform event.
///
/// Only the fields relevant to \ref type are meaningful; every other field keeps
/// the default value listed here. No pointer into the backend survives the
/// translation, so consumers may copy and retain events freely.
struct PlatformEvent {
    PlatformEventType type = PlatformEventType::Quit;

    /// Physical key, already translated out of the backend's numbering by the
    /// platform layer. Meaningful only for KeyDown / KeyUp.
    Scancode scancode = Scancode::Unknown;

    /// Backend keycode; layout dependent and rarely what a binding wants. Kept
    /// raw (int) on purpose: no other layer should depend on its numbering.
    std::int32_t keycode = 0;
    bool repeat = false;

    MouseButton button = MouseButton::None;
    float x = 0.0f;
    float y = 0.0f;
    float dx = 0.0f;
    float dy = 0.0f;

    std::int32_t width = 0;
    std::int32_t height = 0;

    std::string text;
};

/// \brief Stable, human-readable name of \p type, used by logs and tests.
std::string_view toString(PlatformEventType type) noexcept;

/// \brief Stable, human-readable name of \p button, used by rebinding persistence.
/// Never used for display text; UI localizes its own labels.
std::string_view toString(MouseButton button) noexcept;

/// \brief Inverse of \ref toString(MouseButton). Returns MouseButton::None for text
/// that does not name a button.
MouseButton mouseButtonFromName(std::string_view name) noexcept;

}  // namespace fa::platform
