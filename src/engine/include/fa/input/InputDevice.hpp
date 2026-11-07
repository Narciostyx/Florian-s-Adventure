#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>

#include <fa/core/Math.hpp>
#include <fa/input/Binding.hpp>
#include <fa/platform/PlatformEvent.hpp>
#include <fa/platform/Scancode.hpp>

namespace fa::input {

class KeyboardDevice;
class MouseDevice;

/// \brief Which backend a device speaks for. Used to pick the concrete accessors that
/// expose pointer or text state; deliberately not RTTI (`/GR-`).
enum class InputDeviceKind : std::uint8_t { Keyboard, Mouse, Gamepad };

/// \brief One physical input source. Adding a device means implementing this and calling
/// InputSystem::addDevice(); no gameplay code changes (docs/reference/code-reference.md).
///
/// The frame contract is: `poll()` with this frame's events, consumers query,
/// then `endFrame()` clears the edges. `held()` therefore survives across frames
/// while `pressed()` / `released()` only answer for the frame just polled.
class IInputDevice {
public:
    IInputDevice() = default;
    virtual ~IInputDevice() = default;
    IInputDevice(const IInputDevice&) = delete;
    IInputDevice& operator=(const IInputDevice&) = delete;

    /// \brief Which backend this device speaks for.
    virtual InputDeviceKind kind() const noexcept = 0;

    /// \brief Typed accessors for the two v1 devices. Default `nullptr` means "not me", so a
    /// third-party device never needs a downcast and the engine never needs RTTI.
    virtual KeyboardDevice* asKeyboard() noexcept { return nullptr; }
    virtual MouseDevice* asMouse() noexcept { return nullptr; }

    /// \brief False for binding kinds this device cannot produce, which keeps an
    /// unsupported binding inert instead of silently always-firing.
    virtual bool handles(BindingKind bindingKind) const noexcept = 0;

    /// \brief Consumes one frame of events. Events belonging to other devices are ignored.
    virtual void poll(std::span<const fa::platform::PlatformEvent> events) = 0;

    /// \brief Clears press/release edges and any per-frame accumulation.
    virtual void endFrame() noexcept = 0;

    /// \brief Drops all state, including "held". Called when the window loses focus,
    /// because the matching release events are frequently never delivered.
    virtual void reset() noexcept = 0;

    /// \brief Steady state: the binding's input is currently down.
    virtual bool held(const Binding& binding) const noexcept = 0;
    /// \brief Edge: the binding went down during the frame just polled.
    virtual bool pressed(const Binding& binding) const noexcept = 0;
    /// \brief Edge: the binding came up during the frame just polled.
    virtual bool released(const Binding& binding) const noexcept = 0;

    /// \brief Signed strength in `[-scale, scale]`; 0 when the binding is not held.
    virtual float value(const Binding& binding) const noexcept = 0;
};

/// \brief Keyboard state. `PlatformEvent::scancode` arrives already translated by the
/// platform layer, so nothing here knows the backend's numbering. Overrides inherit
/// the \ref IInputDevice documentation.
class KeyboardDevice final : public IInputDevice {
public:
    /// \cond
    InputDeviceKind kind() const noexcept override { return InputDeviceKind::Keyboard; }
    KeyboardDevice* asKeyboard() noexcept override { return this; }
    bool handles(BindingKind bindingKind) const noexcept override;

    void poll(std::span<const fa::platform::PlatformEvent> events) override;
    void endFrame() noexcept override;
    void reset() noexcept override;

    bool held(const Binding& binding) const noexcept override;
    bool pressed(const Binding& binding) const noexcept override;
    bool released(const Binding& binding) const noexcept override;
    float value(const Binding& binding) const noexcept override;
    /// \endcond

    /// \brief Direct scancode queries, bypassing bindings (debug overlays and the
    /// console hotkey only). @{
    bool held(fa::platform::Scancode scancode) const noexcept;
    bool pressed(fa::platform::Scancode scancode) const noexcept;
    bool released(fa::platform::Scancode scancode) const noexcept;
    /// @}

    /// \brief UTF-8 text typed this frame. Cleared by endFrame(); aimed at the TextInput
    /// context, never at gameplay keys.
    std::string_view text() const noexcept { return m_text; }

private:
    std::unordered_set<fa::platform::Scancode> m_down;
    std::unordered_set<fa::platform::Scancode> m_pressed;
    std::unordered_set<fa::platform::Scancode> m_released;
    std::string m_text;
};

/// \brief Mouse state. Position is in raw window pixels; InputSystem applies the virtual
/// resolution transform before gameplay sees it. Overrides inherit the \ref
/// IInputDevice documentation.
class MouseDevice final : public IInputDevice {
public:
    /// \cond
    InputDeviceKind kind() const noexcept override { return InputDeviceKind::Mouse; }
    MouseDevice* asMouse() noexcept override { return this; }
    bool handles(BindingKind bindingKind) const noexcept override;

    void poll(std::span<const fa::platform::PlatformEvent> events) override;
    void endFrame() noexcept override;
    void reset() noexcept override;

    bool held(const Binding& binding) const noexcept override;
    bool pressed(const Binding& binding) const noexcept override;
    bool released(const Binding& binding) const noexcept override;
    float value(const Binding& binding) const noexcept override;
    /// \endcond

    /// \brief Direct button queries, bypassing bindings. @{
    bool held(fa::platform::MouseButton button) const noexcept;
    bool pressed(fa::platform::MouseButton button) const noexcept;
    bool released(fa::platform::MouseButton button) const noexcept;
    /// @}

    /// \brief Last known cursor position in window pixels, top-left origin, y down.
    fa::core::Vec2 position() const noexcept { return m_position; }

    /// \brief Movement accumulated this frame; cleared by endFrame().
    fa::core::Vec2 delta() const noexcept { return m_delta; }

    /// \brief Wheel movement accumulated this frame as (horizontal, vertical) notches.
    fa::core::Vec2 scroll() const noexcept { return m_scroll; }

private:
    std::unordered_set<fa::platform::MouseButton> m_down;
    std::unordered_set<fa::platform::MouseButton> m_pressed;
    std::unordered_set<fa::platform::MouseButton> m_released;
    fa::core::Vec2 m_position{0.0f, 0.0f};
    fa::core::Vec2 m_delta{0.0f, 0.0f};
    fa::core::Vec2 m_scroll{0.0f, 0.0f};
};

}  // namespace fa::input
