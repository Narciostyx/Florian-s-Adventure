#include <fa/platform/Scancode.hpp>

#include <array>
#include <utility>

namespace fa::platform {
namespace {

/// One row per enumerator, in declaration order, so the table and the enum cannot
/// drift apart: `kNames` is indexed by the enum value and a static_assert pins the
/// size. Names double as the on-disk spelling, hence no spaces or punctuation.
constexpr std::array<std::string_view, kScancodeCount> kNames = {
    "Unknown",

    "A",
    "B",
    "C",
    "D",
    "E",
    "F",
    "G",
    "H",
    "I",
    "J",
    "K",
    "L",
    "M",
    "N",
    "O",
    "P",
    "Q",
    "R",
    "S",
    "T",
    "U",
    "V",
    "W",
    "X",
    "Y",
    "Z",

    "Digit1",
    "Digit2",
    "Digit3",
    "Digit4",
    "Digit5",
    "Digit6",
    "Digit7",
    "Digit8",
    "Digit9",
    "Digit0",

    "Escape",
    "Enter",
    "Backspace",
    "Tab",
    "Space",
    "Minus",
    "Equals",
    "LeftBracket",
    "RightBracket",
    "Backslash",
    "Semicolon",
    "Apostrophe",
    "Grave",
    "Comma",
    "Period",
    "Slash",
    "CapsLock",

    "F1",
    "F2",
    "F3",
    "F4",
    "F5",
    "F6",
    "F7",
    "F8",
    "F9",
    "F10",
    "F11",
    "F12",

    "PrintScreen",
    "ScrollLock",
    "Pause",
    "Insert",
    "Home",
    "PageUp",
    "Delete",
    "End",
    "PageDown",
    "Right",
    "Left",
    "Down",
    "Up",

    "NumLock",
    "KeypadDivide",
    "KeypadMultiply",
    "KeypadMinus",
    "KeypadPlus",
    "KeypadEnter",
    "Keypad1",
    "Keypad2",
    "Keypad3",
    "Keypad4",
    "Keypad5",
    "Keypad6",
    "Keypad7",
    "Keypad8",
    "Keypad9",
    "Keypad0",
    "KeypadPeriod",

    "LeftCtrl",
    "LeftShift",
    "LeftAlt",
    "LeftGui",
    "RightCtrl",
    "RightShift",
    "RightAlt",
    "RightGui",
};

static_assert(kNames.size() == kScancodeCount, "kNames must have one entry per Scancode");
static_assert(static_cast<std::size_t>(Scancode::A) == 1, "offset of the letter block changed");
static_assert(static_cast<std::size_t>(Scancode::RightGui) + 1 == kScancodeCount,
              "trailing block changed; update kNames");

constexpr bool allNamesDistinct() {
    for (std::size_t i = 0; i < kNames.size(); ++i) {
        for (std::size_t j = i + 1; j < kNames.size(); ++j) {
            if (kNames[i] == kNames[j]) {
                return false;
            }
        }
    }
    return true;
}

static_assert(allNamesDistinct(), "duplicate Scancode name would make scancodeFromName ambiguous");

}  // namespace

std::string_view toString(Scancode scancode) noexcept {
    const auto index = static_cast<std::size_t>(scancode);
    return index < kNames.size() ? kNames[index] : kNames[0];
}

Scancode scancodeFromName(std::string_view name) noexcept {
    for (std::size_t i = 0; i < kNames.size(); ++i) {
        if (kNames[i] == name) {
            return static_cast<Scancode>(i);
        }
    }
    return Scancode::Unknown;
}

}  // namespace fa::platform
