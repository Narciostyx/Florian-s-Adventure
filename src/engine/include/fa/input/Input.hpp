#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <fa/core/Math.hpp>
#include <fa/core/Result.hpp>
#include <fa/input/Action.hpp>
#include <fa/input/ActionMap.hpp>
#include <fa/input/Binding.hpp>
#include <fa/input/InputContext.hpp>
#include <fa/input/InputDevice.hpp>
#include <fa/platform/PlatformEvent.hpp>

namespace fa::input {

/// What gameplay is allowed to know about input (docs/modules/07-input.md §5). One
/// narrow interface, no devices, no SDL, no JSON - M09/M16 depend on this and on
/// nothing else, which keeps rebinding and device work free of gameplay changes.
class IInput {
public:
    IInput() = default;
    virtual ~IInput() = default;
    IInput(const IInput&) = delete;
    IInput& operator=(const IInput&) = delete;

    virtual bool isDown(ActionId action) const = 0;
    virtual bool wasPressed(ActionId action) const = 0;
    virtual bool wasReleased(ActionId action) const = 0;

    /// Signed strength of the strongest binding, 0 when nothing is held.
    virtual float axis(ActionId action) const = 0;

    /// Reserved four-direction movement, (x, y) with y pointing down, normalized so
    /// diagonals are not faster. Independent of which keys the project binds.
    virtual fa::core::Vec2 moveAxis() const = 0;

    /// Cursor position in virtual resolution, top-left origin, y down.
    virtual fa::core::Vec2 mousePosition() const = 0;

    virtual void pushContext(InputContext context) = 0;
    virtual void popContext() = 0;

    /// Clears per-frame edges and accumulations. Call once per tick, after every
    /// consumer has read the frame.
    virtual void endFrame() = 0;
};

/// Owns the devices, the per-context ActionMaps and the context stack.
///
/// Frame order is fixed and matters:
///   1. `poll(events)` - devices consume focus/keys/buttons/pointer and text.
///   2. gameplay and UI read actions.
///   3. `endFrame()` - edges, mouse delta, wheel and text are cleared.
/// Reading before `poll()` or after `endFrame()` sees the previous frame's edges
/// (all cleared, so it sees none).
class InputSystem final : public IInput {
public:
    InputSystem();
    ~InputSystem() override;

    InputSystem(const InputSystem&) = delete;
    InputSystem& operator=(const InputSystem&) = delete;

    /// Takes ownership. Order does not matter; one device per kind is expected but
    /// not enforced.
    void addDevice(std::unique_ptr<IInputDevice> device);
    std::size_t deviceCount() const noexcept { return m_devices.size(); }
    bool hasDevice(InputDeviceKind kind) const noexcept;

    /// Null when no keyboard / mouse device was added.
    KeyboardDevice* keyboard() const noexcept { return m_keyboard; }
    MouseDevice* mouse() const noexcept { return m_mouse; }

    /// Feeds one frame of platform events to every device. Focus loss clears all
    /// held state first, because the matching key-up events are often never
    /// delivered (docs/modules/07-input.md §8) and a stuck key is worse than a
    /// missed press.
    void poll(std::span<const fa::platform::PlatformEvent> events);

    /// Queries.
    bool isDown(ActionId action) const override;
    bool wasPressed(ActionId action) const override;
    bool wasReleased(ActionId action) const override;
    float axis(ActionId action) const override;
    fa::core::Vec2 moveAxis() const override;
    fa::core::Vec2 mousePosition() const override;
    void endFrame() override;

    /// Full evaluation of one action, avoiding four separate lookups.
    ActionState actionState(ActionId action) const;

    /// Raw window-pixel cursor position, before the mouse transform.
    fa::core::Vec2 mousePositionRaw() const;

    /// Wheel notches this frame, (horizontal, vertical).
    fa::core::Vec2 scroll() const;

    /// UTF-8 text typed this frame; intended for the TextInput context.
    std::string_view textInput() const;

    /// Maps window pixels to virtual resolution: `virtual = raw * scale + offset`.
    /// Identity until set, which is what tests and UI-only builds want.
    void setMouseTransform(fa::core::Vec2 scale, fa::core::Vec2 offset = fa::core::Vec2{0.0f, 0.0f});
    fa::core::Vec2 mouseScale() const noexcept { return m_mouseScale; }
    fa::core::Vec2 mouseOffset() const noexcept { return m_mouseOffset; }

    /// Context stack.
    void pushContext(InputContext context) override;
    void popContext() override;
    InputContext activeContext() const noexcept { return m_contexts.top(); }
    const InputContextStack& contextStack() const noexcept { return m_contexts; }

    /// The map queries resolve against, i.e. the top of the stack.
    ActionMap& activeMap() noexcept { return m_maps[static_cast<std::size_t>(m_contexts.top())]; }
    const ActionMap& activeMap() const noexcept { return m_maps[static_cast<std::size_t>(m_contexts.top())]; }

    /// Per-context map, for authoring and rebinding screens.
    ActionMap& mapFor(InputContext context) noexcept { return m_maps[static_cast<std::size_t>(context)]; }
    const ActionMap& mapFor(InputContext context) const noexcept { return m_maps[static_cast<std::size_t>(context)]; }

    /// Names for debug output and persistence. Interning is idempotent.
    ActionId registerAction(std::string_view name);
    const ActionRegistry& actions() const noexcept { return m_names; }

    /// Adds a binding to `context`'s map. Returns false when the binding is unusable
    /// (unknown key, no button) so a rebinding UI cannot install a dead key.
    bool bind(InputContext context, ActionId action, Binding binding);
    bool unbind(InputContext context, ActionId action, const Binding& binding);

    /// WASD + arrows + mouse, for Gameplay, plus Escape-to-UI style cursor keys in
    /// UI. Fills only actions that have no binding yet, so player edits win.
    void applyDefaultBindings();

    /// Drops every held key, button and accumulation, keeping bindings and contexts.
    void reset();

    /// Whole-layout JSON: `{ "version": 1, "contexts": { "Gameplay": { ... } } }`.
    std::string saveBindings() const;

    /// Replaces the bindings of every context named in the document. Contexts absent
    /// from the document keep their current bindings.
    fa::core::Status loadBindings(std::string_view json);

    /// Per-context variants used by save/load and by tests.
    std::string saveContextToJson(InputContext context) const;
    fa::core::Status loadContextFromJson(InputContext context, std::string_view json);

private:
    std::vector<std::unique_ptr<IInputDevice>> m_devices;
    KeyboardDevice* m_keyboard = nullptr;
    MouseDevice* m_mouse = nullptr;

    ActionRegistry m_names;
    InputContextStack m_contexts;

    /// Indexed by InputContext; fixed size so the index arithmetic stays trivial.
    std::vector<ActionMap> m_maps;

    fa::core::Vec2 m_mouseScale{1.0f, 1.0f};
    fa::core::Vec2 m_mouseOffset{0.0f, 0.0f};
};

}  // namespace fa::input
