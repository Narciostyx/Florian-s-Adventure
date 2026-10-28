#include <fa/input/Binding.hpp>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace fa::input {
namespace {

constexpr std::string_view kKeyPrefix = "Key:";
constexpr std::string_view kMouseButtonPrefix = "MouseButton:";
constexpr std::string_view kMouseWheelPrefix = "MouseWheel:";
constexpr std::string_view kGamepadButtonPrefix = "GamepadButton:";
constexpr std::string_view kGamepadAxisPrefix = "GamepadAxis:";

bool startsWith(std::string_view text, std::string_view prefix) noexcept {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

/// `%g` keeps the on-disk form short (`-1`, `0.5`) instead of the six trailing
/// zeros std::to_string would produce.
std::string formatScale(float scale) {
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(scale));
    return buffer;
}

void appendScale(std::string& out, float scale) {
    if (scale == 1.0f) {
        return;
    }
    out.push_back(':');
    out += formatScale(scale);
}

/// Splits `a:b:c` into at most three fields. Returns the number of fields found.
std::size_t splitFields(std::string_view text, std::string_view fields[3]) noexcept {
    std::size_t count = 0;
    std::size_t start = 0;
    while (count < 3) {
        const auto colon = text.find(':', start);
        if (colon == std::string_view::npos) {
            fields[count++] = text.substr(start);
            return count;
        }
        fields[count++] = text.substr(start, colon - start);
        start = colon + 1;
    }
    return count;
}

bool parseFloat(std::string_view text, float& out) noexcept {
    if (text.empty()) {
        return false;
    }
    std::string owned{text};
    char* end = nullptr;
    const float parsed = std::strtof(owned.c_str(), &end);
    if (end == owned.c_str() || *end != '\0') {
        return false;
    }
    out = parsed;
    return true;
}

bool parseInt(std::string_view text, int& out) noexcept {
    if (text.empty()) {
        return false;
    }
    std::string owned{text};
    char* end = nullptr;
    const long parsed = std::strtol(owned.c_str(), &end, 10);
    if (end == owned.c_str() || *end != '\0') {
        return false;
    }
    out = static_cast<int>(parsed);
    return true;
}

}  // namespace

bool operator==(const Binding& lhs, const Binding& rhs) noexcept {
    if (lhs.kind != rhs.kind || lhs.scale != rhs.scale) {
        return false;
    }
    switch (lhs.kind) {
        case BindingKind::None:
            return true;
        case BindingKind::Key:
            return lhs.scancode == rhs.scancode;
        case BindingKind::MouseButton:
        case BindingKind::MouseWheel:
            return lhs.mouseButton == rhs.mouseButton;
        case BindingKind::GamepadButton:
        case BindingKind::GamepadAxis:
            return lhs.gamepadControl == rhs.gamepadControl;
    }
    return false;
}

Binding Binding::fromKey(fa::platform::Scancode scancode, float scale) noexcept {
    Binding binding;
    binding.kind = BindingKind::Key;
    binding.scancode = scancode;
    binding.scale = scale;
    return binding;
}

Binding Binding::fromMouseButton(fa::platform::MouseButton button, float scale) noexcept {
    Binding binding;
    binding.kind = BindingKind::MouseButton;
    binding.mouseButton = button;
    binding.scale = scale;
    return binding;
}

std::string Binding::toString() const {
    std::string out;
    switch (kind) {
        case BindingKind::None:
            return out;
        case BindingKind::Key:
            out.assign(kKeyPrefix);
            out += fa::platform::toString(scancode);
            break;
        case BindingKind::MouseButton:
            out.assign(kMouseButtonPrefix);
            out += fa::platform::toString(mouseButton);
            break;
        case BindingKind::MouseWheel:
            out.assign(kMouseWheelPrefix);
            out += fa::platform::toString(mouseButton);
            break;
        case BindingKind::GamepadButton:
            out.assign(kGamepadButtonPrefix);
            out += std::to_string(gamepadControl);
            break;
        case BindingKind::GamepadAxis:
            out.assign(kGamepadAxisPrefix);
            out += std::to_string(gamepadControl);
            break;
    }
    appendScale(out, scale);
    return out;
}

Binding Binding::fromString(std::string_view text) noexcept {
    std::string_view fields[3] = {};
    const auto count = splitFields(text, fields);
    if (count == 0) {
        return Binding{};
    }

    const auto prefix = fields[0];
    std::string_view payload = count >= 2 ? fields[1] : std::string_view{};
    float scale = 1.0f;
    if (count == 3 && !parseFloat(fields[2], scale)) {
        return Binding{};
    }

    if (!prefix.empty() && payload.empty()) {
        return Binding{};
    }

    if (startsWith(text, kKeyPrefix)) {
        const auto scancode = fa::platform::scancodeFromName(payload);
        if (scancode == fa::platform::Scancode::Unknown) {
            return Binding{};
        }
        return Binding::fromKey(scancode, scale);
    }
    if (startsWith(text, kMouseButtonPrefix) || startsWith(text, kMouseWheelPrefix)) {
        const auto button = fa::platform::mouseButtonFromName(payload);
        if (button == fa::platform::MouseButton::None) {
            return Binding{};
        }
        Binding binding = Binding::fromMouseButton(button, scale);
        if (startsWith(text, kMouseWheelPrefix)) {
            binding.kind = BindingKind::MouseWheel;
        }
        return binding;
    }
    if (startsWith(text, kGamepadButtonPrefix) || startsWith(text, kGamepadAxisPrefix)) {
        int control = 0;
        if (!parseInt(payload, control)) {
            return Binding{};
        }
        Binding binding;
        binding.kind = startsWith(text, kGamepadAxisPrefix) ? BindingKind::GamepadAxis : BindingKind::GamepadButton;
        binding.gamepadControl = control;
        binding.scale = scale;
        return binding;
    }
    return Binding{};
}

bool isUsable(const Binding& binding) noexcept {
    switch (binding.kind) {
        case BindingKind::None:
            return false;
        case BindingKind::Key:
            return binding.scancode != fa::platform::Scancode::Unknown;
        case BindingKind::MouseButton:
        case BindingKind::MouseWheel:
            return binding.mouseButton != fa::platform::MouseButton::None;
        case BindingKind::GamepadButton:
        case BindingKind::GamepadAxis:
            return true;
    }
    return false;
}

}  // namespace fa::input
