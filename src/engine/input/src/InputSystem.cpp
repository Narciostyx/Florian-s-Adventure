#include <fa/input/Input.hpp>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include <fa/core/Log.hpp>

namespace fa::input {
namespace {

Binding keyBinding(fa::platform::Scancode scancode, float scale = 1.0f) {
    return Binding::fromKey(scancode, scale);
}

Binding mouseBinding(fa::platform::MouseButton button, float scale = 1.0f) {
    return Binding::fromMouseButton(button, scale);
}

}  // namespace

InputSystem::InputSystem() : m_maps(kInputContextCount) {
    fa::core::logf(
        fa::core::LogCategory::Input, fa::core::LogLevel::Debug, "input: %zu context(s) initialised", m_maps.size());
}

InputSystem::~InputSystem() = default;

void InputSystem::addDevice(std::unique_ptr<IInputDevice> device) {
    if (!device) {
        return;
    }
    if (auto* keyboard = device->asKeyboard(); keyboard != nullptr) {
        m_keyboard = keyboard;
    } else if (auto* mouse = device->asMouse(); mouse != nullptr) {
        m_mouse = mouse;
    }
    m_devices.push_back(std::move(device));
}

bool InputSystem::hasDevice(InputDeviceKind kind) const noexcept {
    for (const auto& device : m_devices) {
        if (device->kind() == kind) {
            return true;
        }
    }
    return false;
}

void InputSystem::poll(std::span<const fa::platform::PlatformEvent> events) {
    for (const auto& device : m_devices) {
        device->poll(events);
    }
}

ActionState InputSystem::actionState(ActionId action) const {
    ActionState state;
    if (action == kInvalidActionId) {
        return state;
    }
    for (const auto& binding : activeMap().bindings(action)) {
        for (const auto& device : m_devices) {
            if (!device->handles(binding.kind)) {
                continue;
            }
            /// Edges are evaluated per binding, not per action: "any binding pressed"
            /// is the OR the module doc asks for.
            if (device->pressed(binding)) {
                state.wasPressed = true;
            }
            if (device->released(binding)) {
                state.wasReleased = true;
            }
            if (!device->held(binding)) {
                continue;
            }
            state.isDown = true;
            const float value = device->value(binding);
            /// Largest magnitude wins so a stick deflected fully dominates a keyboard
            /// key on the same action, and `-1` scales keep their sign.
            if (std::fabs(value) > std::fabs(state.value)) {
                state.value = value;
            }
        }
    }
    return state;
}

bool InputSystem::isDown(ActionId action) const {
    return actionState(action).isDown;
}

bool InputSystem::wasPressed(ActionId action) const {
    return actionState(action).wasPressed;
}

bool InputSystem::wasReleased(ActionId action) const {
    return actionState(action).wasReleased;
}

float InputSystem::axis(ActionId action) const {
    return actionState(action).value;
}

fa::core::Vec2 InputSystem::moveAxis() const {
    const float x = axis(kActionMoveRight) - axis(kActionMoveLeft);
    const float y = axis(kActionMoveDown) - axis(kActionMoveUp);
    fa::core::Vec2 result{x, y};
    /// Y points down (pixel-metrics.md), so "up" subtracts. Normalising only above
    /// unit length keeps the result proportional while making diagonals not faster.
    const float lengthSquared = result.x * result.x + result.y * result.y;
    if (lengthSquared > 1.0f) {
        result /= std::sqrt(lengthSquared);
    }
    return result;
}

fa::core::Vec2 InputSystem::mousePosition() const {
    return mousePositionRaw() * m_mouseScale + m_mouseOffset;
}

fa::core::Vec2 InputSystem::mousePositionRaw() const {
    return m_mouse == nullptr ? fa::core::Vec2{0.0f, 0.0f} : m_mouse->position();
}

fa::core::Vec2 InputSystem::scroll() const {
    return m_mouse == nullptr ? fa::core::Vec2{0.0f, 0.0f} : m_mouse->scroll();
}

std::string_view InputSystem::textInput() const {
    return m_keyboard == nullptr ? std::string_view{} : m_keyboard->text();
}

void InputSystem::setMouseTransform(fa::core::Vec2 scale, fa::core::Vec2 offset) {
    m_mouseScale = scale;
    m_mouseOffset = offset;
}

void InputSystem::endFrame() {
    for (const auto& device : m_devices) {
        device->endFrame();
    }
}

void InputSystem::pushContext(InputContext context) {
    m_contexts.push(context);
    const auto name = toString(context);
    fa::core::logf(fa::core::LogCategory::Input,
                   fa::core::LogLevel::Trace,
                   "input: pushed context %.*s (depth %zu)",
                   static_cast<int>(name.size()),
                   name.data(),
                   m_contexts.size());
}

void InputSystem::popContext() {
    const auto active = m_contexts.pop();
    const auto name = toString(active);
    fa::core::logf(fa::core::LogCategory::Input,
                   fa::core::LogLevel::Trace,
                   "input: popped to context %.*s (depth %zu)",
                   static_cast<int>(name.size()),
                   name.data(),
                   m_contexts.size());
}

ActionId InputSystem::registerAction(std::string_view name) {
    return m_names.intern(name);
}

bool InputSystem::bind(InputContext context, ActionId action, Binding binding) {
    if (!isUsable(binding) || action == kInvalidActionId) {
        return false;
    }
    mapFor(context).bind(action, binding);
    return true;
}

bool InputSystem::unbind(InputContext context, ActionId action, const Binding& binding) {
    return mapFor(context).unbind(action, binding);
}

void InputSystem::applyDefaultBindings() {
    struct DefaultBinding {
        InputContext context;
        std::string_view action;
        Binding binding;
    };

    const DefaultBinding defaults[] = {
        {InputContext::Gameplay, kNameMoveLeft, keyBinding(fa::platform::Scancode::A)},
        {InputContext::Gameplay, kNameMoveLeft, keyBinding(fa::platform::Scancode::Left)},
        {InputContext::Gameplay, kNameMoveRight, keyBinding(fa::platform::Scancode::D)},
        {InputContext::Gameplay, kNameMoveRight, keyBinding(fa::platform::Scancode::Right)},
        {InputContext::Gameplay, kNameMoveUp, keyBinding(fa::platform::Scancode::W)},
        {InputContext::Gameplay, kNameMoveUp, keyBinding(fa::platform::Scancode::Up)},
        {InputContext::Gameplay, kNameMoveDown, keyBinding(fa::platform::Scancode::S)},
        {InputContext::Gameplay, kNameMoveDown, keyBinding(fa::platform::Scancode::Down)},
        {InputContext::Gameplay, "Interact", keyBinding(fa::platform::Scancode::E)},
        {InputContext::Gameplay, "Interact", mouseBinding(fa::platform::MouseButton::Left)},
        {InputContext::Gameplay, "Cancel", keyBinding(fa::platform::Scancode::Escape)},
        {InputContext::UI, "Confirm", keyBinding(fa::platform::Scancode::Enter)},
        {InputContext::UI, "Confirm", keyBinding(fa::platform::Scancode::Space)},
        {InputContext::UI, "Cancel", keyBinding(fa::platform::Scancode::Escape)},
    };

    /// Player (or project) edits win: an action that already had at least one binding
    /// before this call is left completely alone, including its remaining defaults.
    std::vector<std::pair<InputContext, ActionId>> seeded;
    for (const auto& entry : defaults) {
        const auto action = registerAction(entry.action);
        const auto key = std::pair{entry.context, action};
        const bool seen = std::find(seeded.begin(), seeded.end(), key) != seeded.end();
        if (!seen) {
            if (!mapFor(entry.context).bindings(action).empty()) {
                continue;
            }
            seeded.push_back(key);
        }
        mapFor(entry.context).bind(action, entry.binding);
    }
}

void InputSystem::reset() {
    for (const auto& device : m_devices) {
        device->reset();
    }
}

std::string InputSystem::saveBindings() const {
    return saveAllActionMapsToJson(m_maps, m_names);
}

fa::core::Status InputSystem::loadBindings(std::string_view json) {
    return loadAllActionMapsFromJson(json, m_maps, m_names);
}

std::string InputSystem::saveContextToJson(InputContext context) const {
    return saveActionMapToJson(mapFor(context), m_names);
}

fa::core::Status InputSystem::loadContextFromJson(InputContext context, std::string_view json) {
    return loadActionMapFromJson(json, mapFor(context), m_names);
}

}  // namespace fa::input
