/// \file
/// \brief Tests for M07: scancodes, bindings, action maps, context stack and the
/// InputSystem that ties them together.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <fa/core/Error.hpp>
#include <fa/core/Math.hpp>
#include <fa/input/Action.hpp>
#include <fa/input/ActionMap.hpp>
#include <fa/input/Binding.hpp>
#include <fa/input/Input.hpp>
#include <fa/input/InputContext.hpp>
#include <fa/input/InputDevice.hpp>
#include <fa/platform/PlatformEvent.hpp>
#include <fa/platform/Scancode.hpp>

namespace {

using fa::core::Vec2;
using fa::input::ActionId;
using fa::input::ActionMap;
using fa::input::ActionRegistry;
using fa::input::Binding;
using fa::input::BindingKind;
using fa::input::InputContext;
using fa::input::InputContextStack;
using fa::input::InputSystem;
using fa::platform::MouseButton;
using fa::platform::PlatformEvent;
using fa::platform::PlatformEventType;
using fa::platform::Scancode;

PlatformEvent keyEvent(PlatformEventType type, Scancode scancode, bool repeat = false) {
    PlatformEvent event;
    event.type = type;
    event.scancode = scancode;
    event.repeat = repeat;
    return event;
}

PlatformEvent keyDown(Scancode scancode, bool repeat = false) {
    return keyEvent(PlatformEventType::KeyDown, scancode, repeat);
}

PlatformEvent keyUp(Scancode scancode) {
    return keyEvent(PlatformEventType::KeyUp, scancode);
}

PlatformEvent mouseButtonEvent(PlatformEventType type, MouseButton button) {
    PlatformEvent event;
    event.type = type;
    event.button = button;
    return event;
}

PlatformEvent focusLost() {
    PlatformEvent event;
    event.type = PlatformEventType::FocusLost;
    return event;
}

/// Installs the two v1 devices and the stock bindings, which is the configuration the
/// app and every integration test uses. Takes a reference because InputSystem owns
/// devices and is deliberately non-copyable and non-movable.
void configure(InputSystem& input) {
    input.addDevice(std::make_unique<fa::input::KeyboardDevice>());
    input.addDevice(std::make_unique<fa::input::MouseDevice>());
    input.applyDefaultBindings();
}

bool nearlyEqual(float lhs, float rhs) {
    return std::fabs(lhs - rhs) < 1e-5f;
}

}  // namespace

TEST_CASE("Scancode names round-trip for every enumerator", "[input]") {
    for (std::size_t i = 0; i < fa::platform::kScancodeCount; ++i) {
        const auto scancode = static_cast<Scancode>(i);
        const auto name = fa::platform::toString(scancode);
        REQUIRE_FALSE(name.empty());
        CHECK(fa::platform::scancodeFromName(name) == scancode);
    }
}

TEST_CASE("Scancode names are case sensitive and unknown input is Unknown", "[input]") {
    CHECK(fa::platform::scancodeFromName("A") == Scancode::A);
    CHECK(fa::platform::scancodeFromName("a") == Scancode::Unknown);
    CHECK(fa::platform::scancodeFromName("") == Scancode::Unknown);
    CHECK(fa::platform::scancodeFromName("NotAKey") == Scancode::Unknown);
    CHECK(fa::platform::toString(Scancode::Unknown) == "Unknown");

    /// An out-of-range value must not read past the name table.
    const auto beyond = static_cast<Scancode>(fa::platform::kScancodeCount + 7);
    CHECK(fa::platform::toString(beyond) == "Unknown");
}

TEST_CASE("Mouse button names round-trip", "[input]") {
    for (const auto button :
         {MouseButton::Left, MouseButton::Middle, MouseButton::Right, MouseButton::X1, MouseButton::X2}) {
        const auto name = fa::platform::toString(button);
        CHECK(fa::platform::mouseButtonFromName(name) == button);
    }
    CHECK(fa::platform::mouseButtonFromName("Nope") == MouseButton::None);
    CHECK(fa::platform::toString(MouseButton::None) == "None");
}

TEST_CASE("Action ids are stable and empty names are rejected", "[input]") {
    CHECK(fa::input::actionIdFromName("") == fa::input::kInvalidActionId);
    CHECK(fa::input::actionIdFromName("Attack") == fa::input::actionIdFromName("Attack"));
    CHECK(fa::input::actionIdFromName("Attack") != fa::input::actionIdFromName("attack"));
    CHECK(fa::input::actionIdFromName("Attack") != fa::input::kInvalidActionId);
    CHECK(fa::input::kActionMoveLeft == fa::input::actionIdFromName("MoveLeft"));
}

TEST_CASE("ActionRegistry keeps names and ignores empty input", "[input]") {
    ActionRegistry registry;
    const auto attack = registry.intern("Attack");
    CHECK(registry.intern("Attack") == attack);
    CHECK(registry.size() == 1);
    REQUIRE(registry.name(attack) != nullptr);
    CHECK(*registry.name(attack) == "Attack");
    CHECK(registry.nameView(attack) == "Attack");
    CHECK(registry.name(fa::input::kInvalidActionId) == nullptr);
    CHECK(registry.nameView(fa::input::actionIdFromName("Missing")).empty());
    CHECK(registry.intern("") == fa::input::kInvalidActionId);
    CHECK(registry.size() == 1);
}

TEST_CASE("Binding text round-trips", "[input]") {
    const auto a = Binding::fromKey(Scancode::A);
    CHECK(a.toString() == "Key:A");
    CHECK(Binding::fromString("Key:A") == a);

    const auto left = Binding::fromMouseButton(MouseButton::Left);
    CHECK(left.toString() == "MouseButton:Left");
    CHECK(Binding::fromString("MouseButton:Left") == left);

    const auto inverted = Binding::fromKey(Scancode::Left, -1.0f);
    CHECK(inverted.toString() == "Key:Left:-1");
    const auto parsedInverted = Binding::fromString("Key:Left:-1");
    CHECK(parsedInverted.kind == BindingKind::Key);
    CHECK(parsedInverted.scancode == Scancode::Left);
    CHECK(nearlyEqual(parsedInverted.scale, -1.0f));

    /// Reserved for a device that does not exist yet, but must survive a save/load now.
    const auto axis = Binding::fromString("GamepadAxis:2:0.5");
    CHECK(axis.kind == BindingKind::GamepadAxis);
    CHECK(axis.gamepadControl == 2);
    CHECK(nearlyEqual(axis.scale, 0.5f));
}

TEST_CASE("Unparseable bindings degrade to None", "[input]") {
    for (const auto text :
         {"", "A", "Key:", "Key:NotAKey", "MouseButton:Nope", "Nonsense:1", "Key:A:notanumber", "GamepadAxis:x"}) {
        const auto binding = Binding::fromString(text);
        CHECK(binding.kind == BindingKind::None);
        CHECK_FALSE(fa::input::isUsable(binding));
    }
    CHECK_FALSE(fa::input::isUsable(Binding{}));
    CHECK(fa::input::isUsable(Binding::fromKey(Scancode::Space)));
}

TEST_CASE("ActionMap deduplicates, unbinds and clears", "[input]") {
    ActionMap map;
    const auto action = fa::input::actionIdFromName("Attack");

    map.bind(action, Binding::fromKey(Scancode::Space));
    map.bind(action, Binding::fromKey(Scancode::Space));
    CHECK(map.bindings(action).size() == 1);

    map.bind(action, Binding::fromMouseButton(MouseButton::Left));
    CHECK(map.bindings(action).size() == 2);
    CHECK(map.actionCount() == 1);

    CHECK(map.unbind(action, Binding::fromKey(Scancode::Space)));
    CHECK_FALSE(map.unbind(action, Binding::fromKey(Scancode::Space)));
    CHECK(map.bindings(action).size() == 1);

    /// Removing the last binding drops the action, so actionCount() stays meaningful.
    CHECK(map.unbind(action, Binding::fromMouseButton(MouseButton::Left)));
    CHECK(map.actionCount() == 0);
    CHECK(map.bindings(action).empty());

    map.bind(action, Binding::fromKey(Scancode::Q));
    map.clearAction(action);
    CHECK(map.empty());

    map.bind(action, Binding::fromKey(Scancode::Q));
    map.clear();
    CHECK(map.empty());
    CHECK_FALSE(map.unbind(fa::input::kInvalidActionId, Binding{}));
}

TEST_CASE("ActionMap ignores the invalid action id", "[input]") {
    ActionMap map;
    map.bind(fa::input::kInvalidActionId, Binding::fromKey(Scancode::A));
    CHECK(map.empty());
    CHECK(map.bindings(fa::input::kInvalidActionId).empty());
}

TEST_CASE("ActionMap JSON round-trips", "[input]") {
    ActionRegistry names;
    ActionMap map;
    const auto moveLeft = names.intern("MoveLeft");
    const auto attack = names.intern("Attack");
    map.bind(moveLeft, Binding::fromKey(Scancode::A));
    map.bind(moveLeft, Binding::fromKey(Scancode::Left));
    map.bind(attack, Binding::fromMouseButton(MouseButton::Left));

    const auto json = fa::input::saveActionMapToJson(map, names);
    CHECK(json.find("\"MoveLeft\"") != std::string::npos);
    CHECK(json.find("\"Key:A\"") != std::string::npos);

    ActionRegistry loadedNames;
    ActionMap loaded;
    const auto status = fa::input::loadActionMapFromJson(json, loaded, loadedNames);
    REQUIRE(status.isOk());
    CHECK(loaded.bindings(moveLeft).size() == 2);
    CHECK(loaded.bindings(attack).size() == 1);
    CHECK(loadedNames.nameView(moveLeft) == "MoveLeft");

    /// Loading replaces, it does not merge into the existing map.
    ActionMap second;
    second.bind(moveLeft, Binding::fromKey(Scancode::Z));
    REQUIRE(fa::input::loadActionMapFromJson(json, second, loadedNames).isOk());
    CHECK(second.bindings(moveLeft).size() == 2);
    CHECK(loaded.bindings(moveLeft)[0] == Binding::fromKey(Scancode::A));
}

TEST_CASE("Actions without a registered name are not serialized", "[input]") {
    ActionRegistry names;
    ActionMap map;
    const auto anonymous = fa::input::actionIdFromName("Anonymous");
    map.bind(anonymous, Binding::fromKey(Scancode::A));

    const auto json = fa::input::saveActionMapToJson(map, names);
    CHECK(json.find("Anonymous") == std::string::npos);
    CHECK(json == "{}");
}

TEST_CASE("Binding JSON load is lenient and rejects non-objects", "[input]") {
    ActionRegistry names;
    ActionMap map;
    const auto attack = names.intern("Attack");
    map.bind(attack, Binding::fromKey(Scancode::A));

    /// A typo in one entry must not discard the entries that are still valid.
    const auto status = fa::input::loadActionMapFromJson(
        R"({"Attack": ["Key:B", "Key:NotAKey", 42, "MouseButton:Right"]})", map, names);
    REQUIRE(status.isOk());
    REQUIRE(map.bindings(attack).size() == 2);
    CHECK(map.bindings(attack)[0] == Binding::fromKey(Scancode::B));
    CHECK(map.bindings(attack)[1] == Binding::fromMouseButton(MouseButton::Right));

    ActionMap untouched;
    untouched.bind(attack, Binding::fromKey(Scancode::A));
    const auto badJson = fa::input::loadActionMapFromJson("{not json", untouched, names);
    CHECK(badJson.isError());
    CHECK(badJson.error().code == fa::core::error_code::kParseFailure);
    CHECK(untouched.bindings(attack).size() == 1);

    CHECK(fa::input::loadActionMapFromJson("[]", untouched, names).isError());
}

TEST_CASE("Action map file omits empty contexts", "[input]") {
    ActionRegistry names;
    std::vector<ActionMap> maps(fa::input::kInputContextCount);
    const auto moveLeft = names.intern("MoveLeft");
    maps[static_cast<std::size_t>(InputContext::Gameplay)].bind(moveLeft, Binding::fromKey(Scancode::A));

    const auto json = fa::input::saveAllActionMapsToJson(maps, names);
    CHECK(json.find("\"version\"") != std::string::npos);
    CHECK(json.find("\"Gameplay\"") != std::string::npos);
    CHECK(json.find("\"UI\"") == std::string::npos);

    std::vector<ActionMap> loaded(fa::input::kInputContextCount);
    REQUIRE(fa::input::loadAllActionMapsFromJson(json, loaded, names).isOk());
    CHECK(loaded[static_cast<std::size_t>(InputContext::Gameplay)].bindings(moveLeft).size() == 1);
}

TEST_CASE("Whole-layout load is a merge that ignores unknown contexts", "[input]") {
    ActionRegistry names;
    std::vector<ActionMap> maps(fa::input::kInputContextCount);
    const auto moveLeft = names.intern("MoveLeft");
    const auto confirm = names.intern("Confirm");
    maps[static_cast<std::size_t>(InputContext::UI)].bind(confirm, Binding::fromKey(Scancode::Enter));

    const auto json = R"({
        "version": 1,
        "contexts": {
            "Gameplay": { "MoveLeft": ["Key:A"] },
            "NotAContext": { "MoveLeft": ["Key:Z"] }
        }
    })";
    REQUIRE(fa::input::loadAllActionMapsFromJson(json, maps, names).isOk());

    const auto gameplay = static_cast<std::size_t>(InputContext::Gameplay);
    const auto ui = static_cast<std::size_t>(InputContext::UI);
    CHECK(maps[gameplay].bindings(moveLeft).size() == 1);
    CHECK(maps[gameplay].bindings(moveLeft)[0] == Binding::fromKey(Scancode::A));
    /// UI is absent from the document, so it must keep what it had.
    CHECK(maps[ui].bindings(confirm).size() == 1);

    std::vector<ActionMap> target(fa::input::kInputContextCount);
    CHECK(fa::input::loadAllActionMapsFromJson("{\"version\": 1}", target, names).isError());
    CHECK(fa::input::loadAllActionMapsFromJson("[]", target, names).isError());
}

TEST_CASE("InputContext names round-trip", "[input]") {
    for (std::size_t i = 0; i < fa::input::kInputContextCount; ++i) {
        const auto context = static_cast<InputContext>(i);
        const auto name = fa::input::toString(context);
        CHECK(fa::input::inputContextFromName(name) == context);
    }
    CHECK(fa::input::inputContextFromName("Nope") == InputContext::Gameplay);
    CHECK(fa::input::kInputContextCount == 5);
}

TEST_CASE("Context stack starts on Gameplay and never empties", "[input]") {
    InputContextStack stack;
    CHECK(stack.size() == 1);
    CHECK(stack.top() == InputContext::Gameplay);

    stack.push(InputContext::UI);
    CHECK(stack.top() == InputContext::UI);
    CHECK(stack.contains(InputContext::UI));
    CHECK(stack.contains(InputContext::Gameplay));

    /// Popping the base entry is refused, otherwise queries would have no map.
    CHECK(stack.pop() == InputContext::Gameplay);
    CHECK(stack.size() == 1);
    CHECK(stack.top() == InputContext::Gameplay);
    CHECK_FALSE(stack.contains(InputContext::UI));

    stack.push(InputContext::Dialogue);
    stack.push(InputContext::TextInput);
    CHECK(stack.size() == 3);
    CHECK_FALSE(stack.tryPop(InputContext::Dialogue));
    CHECK(stack.tryPop(InputContext::TextInput));
    CHECK(stack.top() == InputContext::Dialogue);
    CHECK(stack.pop() == InputContext::Gameplay);
    CHECK_FALSE(stack.contains(InputContext::Dialogue));

    stack.push(InputContext::Editor);
    stack.clear();
    CHECK(stack.size() == 1);
    CHECK(stack.top() == InputContext::Gameplay);

    /// InputContext::Count is not a usable context.
    stack.push(InputContext::Count);
    CHECK(stack.size() == 1);
}

TEST_CASE("KeyboardDevice tracks held and edges", "[input]") {
    fa::input::KeyboardDevice keyboard;
    CHECK(keyboard.handles(BindingKind::Key));
    CHECK_FALSE(keyboard.handles(BindingKind::MouseButton));

    const std::vector events{keyDown(Scancode::W)};
    keyboard.poll(events);
    CHECK(keyboard.held(Scancode::W));
    CHECK(keyboard.pressed(Scancode::W));
    CHECK_FALSE(keyboard.released(Scancode::W));

    /// Edges last exactly one frame; held state does not.
    keyboard.endFrame();
    CHECK(keyboard.held(Scancode::W));
    CHECK_FALSE(keyboard.pressed(Scancode::W));

    const std::vector release{keyUp(Scancode::W)};
    keyboard.poll(release);
    CHECK_FALSE(keyboard.held(Scancode::W));
    CHECK(keyboard.released(Scancode::W));

    keyboard.endFrame();
    CHECK_FALSE(keyboard.released(Scancode::W));
}

TEST_CASE("Auto-repeat does not produce a second press edge", "[input]") {
    fa::input::KeyboardDevice keyboard;
    const std::vector first{keyDown(Scancode::Space)};
    keyboard.poll(first);
    CHECK(keyboard.pressed(Scancode::Space));
    keyboard.endFrame();

    const std::vector repeated{keyDown(Scancode::Space, true)};
    keyboard.poll(repeated);
    CHECK(keyboard.held(Scancode::Space));
    CHECK_FALSE(keyboard.pressed(Scancode::Space));
}

TEST_CASE("A press and release inside one frame reports both edges", "[input]") {
    fa::input::KeyboardDevice keyboard;
    const std::vector events{keyDown(Scancode::E), keyUp(Scancode::E)};
    keyboard.poll(events);
    CHECK_FALSE(keyboard.held(Scancode::E));
    CHECK(keyboard.pressed(Scancode::E));
    CHECK(keyboard.released(Scancode::E));
}

TEST_CASE("Keyboard text input accumulates and clears", "[input]") {
    fa::input::KeyboardDevice keyboard;

    PlatformEvent first;
    first.type = PlatformEventType::TextInput;
    first.text = "he";
    PlatformEvent second;
    second.type = PlatformEventType::TextInput;
    second.text = "y";
    const std::vector events{first, second};

    keyboard.poll(events);
    CHECK(keyboard.text() == "hey");
    keyboard.endFrame();
    CHECK(keyboard.text().empty());
}

TEST_CASE("Focus loss drops every held key", "[input]") {
    fa::input::KeyboardDevice keyboard;
    const std::vector down{keyDown(Scancode::A)};
    keyboard.poll(down);
    REQUIRE(keyboard.held(Scancode::A));

    /// The matching key-up is frequently never delivered, so a focus loss must clear.
    const std::vector lost{focusLost()};
    keyboard.poll(lost);
    CHECK_FALSE(keyboard.held(Scancode::A));
}

TEST_CASE("MouseDevice tracks buttons, position, delta and wheel", "[input]") {
    fa::input::MouseDevice mouse;
    CHECK(mouse.handles(BindingKind::MouseButton));
    /// Wheel bindings stay inert in v1; scrolling is a delta, not a held state.
    CHECK_FALSE(mouse.handles(BindingKind::MouseWheel));

    PlatformEvent move;
    move.type = PlatformEventType::MouseMove;
    move.x = 120.0f;
    move.y = 80.0f;
    move.dx = 5.0f;
    move.dy = -2.0f;

    PlatformEvent wheel;
    wheel.type = PlatformEventType::MouseWheel;
    wheel.y = 1.0f;

    const std::vector events{mouseButtonEvent(PlatformEventType::MouseDown, MouseButton::Left), move, wheel};
    mouse.poll(events);
    CHECK(mouse.held(MouseButton::Left));
    CHECK(mouse.pressed(MouseButton::Left));
    CHECK(nearlyEqual(mouse.position().x, 120.0f));
    CHECK(nearlyEqual(mouse.position().y, 80.0f));
    CHECK(nearlyEqual(mouse.delta().x, 5.0f));
    CHECK(nearlyEqual(mouse.delta().y, -2.0f));
    CHECK(nearlyEqual(mouse.scroll().y, 1.0f));

    mouse.endFrame();
    CHECK(mouse.held(MouseButton::Left));
    CHECK_FALSE(mouse.pressed(MouseButton::Left));
    CHECK(nearlyEqual(mouse.delta().x, 0.0f));
    CHECK(nearlyEqual(mouse.scroll().y, 0.0f));
    /// Position is absolute state and survives the frame boundary.
    CHECK(nearlyEqual(mouse.position().x, 120.0f));

    const std::vector up{mouseButtonEvent(PlatformEventType::MouseUp, MouseButton::Left)};
    mouse.poll(up);
    CHECK_FALSE(mouse.held(MouseButton::Left));
    CHECK(mouse.released(MouseButton::Left));
}

TEST_CASE("InputSystem reports one action per OR-ed binding", "[input]") {
    InputSystem input;
    configure(input);
    CHECK(input.deviceCount() == 2);
    CHECK(input.hasDevice(fa::input::InputDeviceKind::Keyboard));
    CHECK(input.hasDevice(fa::input::InputDeviceKind::Mouse));
    REQUIRE(input.keyboard() != nullptr);
    REQUIRE(input.mouse() != nullptr);

    /// MoveLeft is bound to A and to Left; either one satisfies the action.
    const std::vector aKey{keyDown(Scancode::A)};
    input.poll(aKey);
    CHECK(input.isDown(fa::input::kActionMoveLeft));
    CHECK(input.wasPressed(fa::input::kActionMoveLeft));
    input.endFrame();

    const std::vector releases{keyUp(Scancode::A)};
    input.poll(releases);
    CHECK_FALSE(input.isDown(fa::input::kActionMoveLeft));
    CHECK(input.wasReleased(fa::input::kActionMoveLeft));

    const std::vector arrow{keyDown(Scancode::Left)};
    input.poll(arrow);
    CHECK(input.isDown(fa::input::kActionMoveLeft));
}

TEST_CASE("Edges survive only the frame that produced them", "[input]") {
    InputSystem input;
    configure(input);
    const std::vector down{keyDown(Scancode::A)};
    input.poll(down);
    CHECK(input.wasPressed(fa::input::kActionMoveLeft));
    input.endFrame();

    input.poll(std::span<const PlatformEvent>{});
    CHECK(input.isDown(fa::input::kActionMoveLeft));
    CHECK_FALSE(input.wasPressed(fa::input::kActionMoveLeft));
    input.endFrame();
}

TEST_CASE("moveAxis is normalised and points down for south", "[input]") {
    InputSystem input;
    configure(input);

    const std::vector down{keyDown(Scancode::W), keyDown(Scancode::D)};
    input.poll(down);
    const auto diagonal = input.moveAxis();
    CHECK(diagonal.x > 0.0f);
    CHECK(diagonal.y < 0.0f);
    CHECK(nearlyEqual(diagonal.x, diagonal.y * -1.0f));
    /// Diagonals must not be faster than cardinals.
    CHECK(std::fabs(glm::length(diagonal) - 1.0f) < 1e-5f);
    input.endFrame();

    const std::vector up{keyDown(Scancode::S), keyUp(Scancode::W), keyUp(Scancode::D)};
    input.poll(up);
    const auto south = input.moveAxis();
    CHECK(nearlyEqual(south.x, 0.0f));
    CHECK(nearlyEqual(south.y, 1.0f));
    CHECK(nearlyEqual(glm::length(south), 1.0f));
}

TEST_CASE("The context stack masks the gameplay map", "[input]") {
    InputSystem input;
    configure(input);
    const std::vector down{keyDown(Scancode::A)};
    input.poll(down);
    REQUIRE(input.isDown(fa::input::kActionMoveLeft));

    /// Opening a menu must stop the character even while the key stays held.
    input.pushContext(InputContext::UI);
    CHECK(input.activeContext() == InputContext::UI);
    CHECK_FALSE(input.isDown(fa::input::kActionMoveLeft));
    CHECK(nearlyEqual(input.moveAxis().x, 0.0f));

    /// UI has its own bindings, so Enter still works.
    const std::vector confirm{keyDown(Scancode::Enter)};
    input.poll(confirm);
    CHECK(input.isDown(fa::input::actionIdFromName("Confirm")));
    input.endFrame();

    input.popContext();
    CHECK(input.activeContext() == InputContext::Gameplay);
    CHECK(input.isDown(fa::input::kActionMoveLeft));
}

TEST_CASE("Axis picks the strongest binding and honours negative scale", "[input]") {
    InputSystem input;
    input.addDevice(std::make_unique<fa::input::KeyboardDevice>());

    const auto throttle = input.registerAction("Throttle");
    input.mapFor(InputContext::Gameplay).bind(throttle, Binding::fromKey(Scancode::W, 0.5f));
    input.mapFor(InputContext::Gameplay).bind(throttle, Binding::fromKey(Scancode::S, -1.0f));

    const std::vector both{keyDown(Scancode::W), keyDown(Scancode::S)};
    input.poll(both);
    /// -1 has the larger magnitude, so it wins over 0.5.
    CHECK(nearlyEqual(input.axis(throttle), -1.0f));

    const std::vector onlyWeak{keyUp(Scancode::S)};
    input.poll(onlyWeak);
    CHECK(nearlyEqual(input.axis(throttle), 0.5f));

    const std::vector released{keyUp(Scancode::W)};
    input.poll(released);
    CHECK(nearlyEqual(input.axis(throttle), 0.0f));
}

TEST_CASE("Unbound actions are inert", "[input]") {
    InputSystem input;
    configure(input);
    const std::vector down{keyDown(Scancode::X)};
    input.poll(down);
    const auto nothing = fa::input::actionIdFromName("NeverBound");
    CHECK_FALSE(input.isDown(nothing));
    CHECK_FALSE(input.wasPressed(nothing));
    CHECK_FALSE(input.wasReleased(nothing));
    CHECK(nearlyEqual(input.axis(nothing), 0.0f));
    CHECK_FALSE(input.isDown(fa::input::kInvalidActionId));
}

TEST_CASE("Rebinding takes effect and rejects unusable bindings", "[input]") {
    InputSystem input;
    configure(input);
    const auto interact = fa::input::actionIdFromName("Interact");

    /// Binding{} is BindingKind::None, so bind() must refuse it instead of installing
    /// a key that can never fire.
    CHECK_FALSE(input.bind(InputContext::Gameplay, interact, Binding{}));

    CHECK(input.unbind(InputContext::Gameplay, interact, Binding::fromKey(Scancode::E)));
    CHECK(input.unbind(InputContext::Gameplay, interact, Binding::fromMouseButton(MouseButton::Left)));

    const std::vector eKey{keyDown(Scancode::E)};
    input.poll(eKey);
    CHECK_FALSE(input.isDown(interact));

    REQUIRE(input.bind(InputContext::Gameplay, interact, Binding::fromKey(Scancode::F)));
    input.poll(eKey);
    CHECK_FALSE(input.isDown(interact));

    const std::vector fKey{keyDown(Scancode::F)};
    input.poll(fKey);
    CHECK(input.isDown(interact));
}

TEST_CASE("Bindings survive a save/load round-trip", "[input]") {
    InputSystem input;
    configure(input);
    const auto json = input.saveBindings();
    CHECK(json.find("\"Gameplay\"") != std::string::npos);
    CHECK(json.find("MoveLeft") != std::string::npos);

    InputSystem restored;
    restored.addDevice(std::make_unique<fa::input::KeyboardDevice>());
    restored.addDevice(std::make_unique<fa::input::MouseDevice>());
    REQUIRE(restored.loadBindings(json).isOk());

    const std::vector down{keyDown(Scancode::A)};
    restored.poll(down);
    CHECK(restored.isDown(fa::input::kActionMoveLeft));

    /// Names travel with the document, so a fresh system can report them.
    CHECK(restored.actions().nameView(fa::input::kActionMoveLeft) == "MoveLeft");

    CHECK(restored.loadBindings("{broken").isError());
}

TEST_CASE("Per-context save and load", "[input]") {
    InputSystem input;
    configure(input);
    const auto json = input.saveContextToJson(InputContext::Gameplay);
    const auto moveUp = fa::input::kActionMoveUp;
    CHECK(input.mapFor(InputContext::Gameplay).bindings(moveUp).size() == 2);

    input.mapFor(InputContext::Gameplay).clear();
    REQUIRE(input.loadContextFromJson(InputContext::Gameplay, json).isOk());
    CHECK(input.mapFor(InputContext::Gameplay).bindings(moveUp).size() == 2);
    /// Loading one context must not touch another.
    CHECK(input.mapFor(InputContext::UI).bindings(fa::input::actionIdFromName("Confirm")).size() == 2);
}

TEST_CASE("Mouse position maps through the virtual resolution transform", "[input]") {
    InputSystem input;
    configure(input);
    CHECK(nearlyEqual(input.mouseScale().x, 1.0f));

    PlatformEvent move;
    move.type = PlatformEventType::MouseMove;
    move.x = 1280.0f;
    move.y = 720.0f;
    const std::vector events{move};
    input.poll(events);
    CHECK(nearlyEqual(input.mousePositionRaw().x, 1280.0f));
    CHECK(nearlyEqual(input.mousePosition().x, 1280.0f));

    /// A 1280x720 window showing a 320x180 virtual screen scales by 1/4.
    input.setMouseTransform(Vec2{0.25f, 0.25f});
    CHECK(nearlyEqual(input.mousePosition().x, 320.0f));
    CHECK(nearlyEqual(input.mousePosition().y, 180.0f));

    input.setMouseTransform(Vec2{0.25f, 0.25f}, Vec2{10.0f, 20.0f});
    CHECK(nearlyEqual(input.mousePosition().x, 330.0f));
    CHECK(nearlyEqual(input.mousePosition().y, 200.0f));
}

TEST_CASE("InputSystem exposes raw scroll and text for the UI layer", "[input]") {
    InputSystem input;
    configure(input);

    PlatformEvent wheel;
    wheel.type = PlatformEventType::MouseWheel;
    wheel.y = 2.0f;
    PlatformEvent text;
    text.type = PlatformEventType::TextInput;
    text.text = "abc";
    const std::vector events{wheel, text};
    input.poll(events);

    CHECK(nearlyEqual(input.scroll().y, 2.0f));
    CHECK(input.textInput() == "abc");

    input.endFrame();
    CHECK(nearlyEqual(input.scroll().y, 0.0f));
    CHECK(input.textInput().empty());
}

TEST_CASE("reset() clears held state but keeps bindings", "[input]") {
    InputSystem input;
    configure(input);
    const std::vector down{keyDown(Scancode::A)};
    input.poll(down);
    REQUIRE(input.isDown(fa::input::kActionMoveLeft));

    input.reset();
    CHECK_FALSE(input.isDown(fa::input::kActionMoveLeft));
    CHECK(input.activeMap().bindings(fa::input::kActionMoveLeft).size() == 2);
}

TEST_CASE("InputSystem without devices answers neutral", "[input]") {
    InputSystem input;
    input.applyDefaultBindings();
    input.poll(std::span<const PlatformEvent>{});
    CHECK_FALSE(input.isDown(fa::input::kActionMoveLeft));
    CHECK(nearlyEqual(input.moveAxis().x, 0.0f));
    CHECK(nearlyEqual(input.mousePosition().x, 0.0f));
    CHECK(nearlyEqual(input.scroll().y, 0.0f));
    CHECK(input.textInput().empty());
    CHECK(input.keyboard() == nullptr);
    CHECK(input.mouse() == nullptr);
}

TEST_CASE("applyDefaultBindings does not overwrite existing bindings", "[input]") {
    InputSystem input;
    input.addDevice(std::make_unique<fa::input::KeyboardDevice>());
    const auto moveLeft = input.registerAction(fa::input::kNameMoveLeft);
    input.mapFor(InputContext::Gameplay).bind(moveLeft, Binding::fromKey(Scancode::J));

    input.applyDefaultBindings();
    /// The player's J binding survives; the stock A/Left pair is not added.
    const auto bindings = input.mapFor(InputContext::Gameplay).bindings(moveLeft);
    REQUIRE(bindings.size() == 1);
    CHECK(bindings[0] == Binding::fromKey(Scancode::J));
}
