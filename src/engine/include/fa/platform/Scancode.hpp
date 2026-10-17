#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace fa::platform {

/// \brief Layout-independent physical key identity, produced by the platform layer.
///
/// The numeric values are this engine's own and carry no relation to SDL's
/// `SDL_Scancode` numbering; `SdlPlatform.cpp` owns the only translation table
/// so that no module outside the platform layer has to know the backend exists.
///
/// Physical (not logical) identity is deliberate: a game binds the key that sits
/// where `W` sits, so WASD keeps its shape on AZERTY. Layout-aware matching is
/// the job of text input, which arrives as character data instead.
///
/// This enum is fixed at build time. Adding keys is a source change plus a row in
/// the translation switch, not a runtime registration.
enum class Scancode : std::uint16_t {
    Unknown = 0,

    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,

    Digit1,
    Digit2,
    Digit3,
    Digit4,
    Digit5,
    Digit6,
    Digit7,
    Digit8,
    Digit9,
    Digit0,

    Escape,
    Enter,
    Backspace,
    Tab,
    Space,
    Minus,
    Equals,
    LeftBracket,
    RightBracket,
    Backslash,
    Semicolon,
    Apostrophe,
    Grave,
    Comma,
    Period,
    Slash,
    CapsLock,

    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,

    PrintScreen,
    ScrollLock,
    Pause,
    Insert,
    Home,
    PageUp,
    Delete,
    End,
    PageDown,
    Right,
    Left,
    Down,
    Up,

    NumLock,
    KeypadDivide,
    KeypadMultiply,
    KeypadMinus,
    KeypadPlus,
    KeypadEnter,
    Keypad1,
    Keypad2,
    Keypad3,
    Keypad4,
    Keypad5,
    Keypad6,
    Keypad7,
    Keypad8,
    Keypad9,
    Keypad0,
    KeypadPeriod,

    LeftCtrl,
    LeftShift,
    LeftAlt,
    LeftGui,
    RightCtrl,
    RightShift,
    RightAlt,
    RightGui,

    Count,
};

/// \brief Number of enumerators in \ref Scancode, including `Unknown`.
inline constexpr std::size_t kScancodeCount = static_cast<std::size_t>(Scancode::Count);

/// \brief Stable, human-readable name; also the spelling used in binding files.
///
/// Returns `"Unknown"` for unlisted values so callers never have to null-check.
std::string_view toString(Scancode scancode) noexcept;

/// \brief Exact-match reverse of \ref toString, used when loading binding files.
///
/// Matching is case sensitive so that a hand-edited config cannot silently bind a
/// different key than it says. Returns `Scancode::Unknown` when unrecognised.
Scancode scancodeFromName(std::string_view name) noexcept;

}  // namespace fa::platform
