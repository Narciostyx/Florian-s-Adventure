#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

#include <fa/core/Hash.hpp>

namespace fa::input {

/// Stable identity of one logical action ("MoveLeft", "Attack", "Interact").
/// Derived from the action name, so gameplay and content files agree without a
/// central registry. 0 is reserved for "no action" and never produced for a
/// non-empty name.
using ActionId = std::uint32_t;

inline constexpr ActionId kInvalidActionId = 0;

/// FNV-1a 64 folded into 32 bits. Stable across runs, machines and compilers, which
/// is what lets bindings live in JSON keyed by action name only. Not
/// collision-resistant against hostile input; action names are authored content.
constexpr ActionId actionIdFromName(std::string_view name) noexcept {
    if (name.empty()) {
        return kInvalidActionId;
    }
    const auto folded = static_cast<ActionId>(fa::core::fnv1a64(name));
    return folded == kInvalidActionId ? 1U : folded;
}

/// Reserved action names the rest of the engine assumes exist. They are ordinary
/// action ids, so a project can rebind them to anything; only the names are fixed.
inline constexpr std::string_view kNameMoveLeft = "MoveLeft";
inline constexpr std::string_view kNameMoveRight = "MoveRight";
inline constexpr std::string_view kNameMoveUp = "MoveUp";
inline constexpr std::string_view kNameMoveDown = "MoveDown";

inline constexpr ActionId kActionMoveLeft = actionIdFromName(kNameMoveLeft);
inline constexpr ActionId kActionMoveRight = actionIdFromName(kNameMoveRight);
inline constexpr ActionId kActionMoveUp = actionIdFromName(kNameMoveUp);
inline constexpr ActionId kActionMoveDown = actionIdFromName(kNameMoveDown);

/// Snapshot of one action for the current frame. Edge flags come from the events
/// polled this frame, so a press-and-release inside a single frame reports both
/// edges while `isDown` stays false.
struct ActionState {
    bool isDown = false;
    bool wasPressed = false;
    bool wasReleased = false;

    /// Signed strength of the winning binding: `isDown` collapsed to the binding's
    /// scale. Negative scales express "this key reverses the axis".
    float value = 0.0f;
};

/// Keeps authored names around for debug overlays and the rebinding screen.
/// Lookups are the only supported access; iteration order is unspecified.
class ActionRegistry {
public:
    /// Returns the id for `name`, remembering the spelling. An empty name yields
    /// kInvalidActionId and is not stored.
    ActionId intern(std::string_view name);

    /// Returns nullptr when `id` was never interned.
    const std::string* name(ActionId id) const noexcept;

    /// Empty view when `id` was never interned.
    std::string_view nameView(ActionId id) const noexcept;

    std::size_t size() const noexcept { return m_names.size(); }
    void clear() noexcept { m_names.clear(); }

private:
    std::unordered_map<ActionId, std::string> m_names;
};

}  // namespace fa::input
