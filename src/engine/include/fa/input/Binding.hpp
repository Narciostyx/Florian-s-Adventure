#pragma once

#include <string>
#include <string_view>

#include <fa/platform/PlatformEvent.hpp>
#include <fa/platform/Scancode.hpp>

namespace fa::input {

/// What a binding listens to. Only `Key` and `MouseButton` are produced in v1; the
/// gamepad members exist so bindings authored today stay parseable once a gamepad
/// device lands (ADR-0002 decision 4). A device answers `false` for kinds it does
/// not handle, so an unsupported binding is inert rather than an error.
enum class BindingKind : unsigned char {
    None,
    Key,
    MouseButton,
    MouseWheel,
    GamepadButton,
    GamepadAxis,
};

/// One physical input feeding an action. `scale` lets a binding push an axis in the
/// opposite direction (`-1.0f`) without a second action.
struct Binding {
    BindingKind kind = BindingKind::None;

    /// Meaningful for BindingKind::Key.
    fa::platform::Scancode scancode = fa::platform::Scancode::Unknown;

    /// Meaningful for BindingKind::MouseButton and BindingKind::MouseWheel.
    fa::platform::MouseButton mouseButton = fa::platform::MouseButton::None;

    /// Meaningful for the Gamepad kinds: backend button or axis index, opaque here.
    int gamepadControl = 0;

    /// Multiplier applied to the raw 0/1 (or -1/1) reading. -1 inverts, 0 disables.
    float scale = 1.0f;

    friend bool operator==(const Binding& lhs, const Binding& rhs) noexcept;
    friend bool operator!=(const Binding& lhs, const Binding& rhs) noexcept { return !(lhs == rhs); }

    static Binding fromKey(fa::platform::Scancode scancode, float scale = 1.0f) noexcept;
    static Binding fromMouseButton(fa::platform::MouseButton button, float scale = 1.0f) noexcept;

    /// Round-trips through JSON as `Key:A`, `MouseButton:Left`, `GamepadAxis:2:-1`.
    /// Parsing only recognizes the spellings `toString(Scancode)` and
    /// `toString(MouseButton)` produce; anything else yields BindingKind::None.
    std::string toString() const;

    /// Returns a binding with BindingKind::None on unknown syntax.
    static Binding fromString(std::string_view text) noexcept;
};

/// True when this binding can never fire, i.e. any v1 query may skip it.
bool isUsable(const Binding& binding) noexcept;

}  // namespace fa::input
