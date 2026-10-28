/// \file
/// \brief Tests for M16: intent-to-velocity movement, facing, state gating, stamina,
/// dodge timers/invulnerability and the input adapter. Everything is headless; the
/// controller writes only `Velocity`, so no physics world or renderer is needed.
/// See docs/build/m16-player-controller.md for the problems found while writing this.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <fa/core/Math.hpp>
#include <fa/input/Action.hpp>
#include <fa/input/Input.hpp>
#include <fa/physics/PhysicsWorld.hpp>
#include <fa/player/PlayerController.hpp>
#include <fa/player/PlayerError.hpp>
#include <fa/player/PlayerSystem.hpp>
#include <fa/player/PlayerTypes.hpp>
#include <fa/scene/CommandBuffer.hpp>
#include <fa/scene/Components.hpp>
#include <fa/scene/World.hpp>
#include <fa/time/FrameContext.hpp>

namespace {

namespace err = fa::player::player_error;

using Catch::Approx;
using fa::core::Vec2;
using fa::physics::Velocity;
using fa::player::Facing;
using fa::player::FacingDir;
using fa::player::Health;
using fa::player::PlayerActionKind;
using fa::player::PlayerController;
using fa::player::PlayerIntent;
using fa::player::PlayerState;
using fa::player::PlayerStateComp;
using fa::player::PlayerSystem;
using fa::player::PlayerTuning;
using fa::player::Stamina;
using fa::scene::EntityId;
using fa::scene::Transform;
using fa::scene::World;

constexpr float kStep = 1.0f / 60.0f;

EntityId makePlayer(World& world, Vec2 position = Vec2{0.0f, 0.0f}, Stamina stamina = Stamina{}) {
    const EntityId id = world.create();
    Transform transform;
    transform.position = position;
    static_cast<void>(world.add<Transform>(id, transform));
    static_cast<void>(world.add<Velocity>(id, Velocity{}));
    PlayerController::attach(world, id, stamina);
    return id;
}

void step(
    PlayerController& controller, World& world, EntityId id, const PlayerIntent& intent, int steps, float dt = kStep) {
    for (int index = 0; index < steps; ++index) {
        controller.update(world, id, intent, dt);
    }
}

Velocity& velocityOf(World& world, EntityId id) {
    return *world.tryGet<Velocity>(id);
}

class FakeInput final : public fa::input::IInput {
public:
    Vec2 move{};

    [[nodiscard]] bool isDown(fa::input::ActionId action) const override { return lookup(down, action); }
    [[nodiscard]] bool wasPressed(fa::input::ActionId action) const override { return lookup(pressed, action); }
    [[nodiscard]] bool wasReleased(fa::input::ActionId) const override { return false; }
    [[nodiscard]] float axis(fa::input::ActionId action) const override { return isDown(action) ? 1.0f : 0.0f; }
    [[nodiscard]] Vec2 moveAxis() const override { return move; }
    [[nodiscard]] Vec2 mousePosition() const override { return Vec2{}; }
    void pushContext(fa::input::InputContext context) override { static_cast<void>(context); }
    void popContext() override {}
    void endFrame() override {}

    std::vector<std::pair<fa::input::ActionId, bool>> down;
    std::vector<std::pair<fa::input::ActionId, bool>> pressed;

private:
    static bool lookup(const std::vector<std::pair<fa::input::ActionId, bool>>& values, fa::input::ActionId action) {
        for (const auto& [id, value] : values) {
            if (id == action) {
                return value;
            }
        }
        return false;
    }
};

}  // namespace

TEST_CASE("Player names are stable", "[player]") {
    REQUIRE(fa::player::toString(Facing::North) == "north");
    REQUIRE(fa::player::toString(Facing::West) == "west");
    REQUIRE(fa::player::toString(Facing::NorthEast) == "north-east");
    REQUIRE(fa::player::toString(Facing::SouthEast) == "south-east");
    REQUIRE(fa::player::toString(Facing::SouthWest) == "south-west");
    REQUIRE(fa::player::toString(Facing::NorthWest) == "north-west");
    REQUIRE(fa::player::toString(PlayerState::Idle) == "idle");
    REQUIRE(fa::player::toString(PlayerState::UseTool) == "use_tool");
    REQUIRE(fa::player::toString(PlayerState::Dead) == "dead");
    REQUIRE(fa::player::toString(PlayerActionKind::Dodge) == "dodge");
    REQUIRE(fa::player::toString(PlayerActionKind::ActionRejected) == "action_rejected");
}

TEST_CASE("Facing helpers pick eight-way sectors", "[player][facing]") {
    REQUIRE(fa::player::facingFromVector(Vec2{1.0f, 0.0f}, Facing::South) == Facing::East);
    REQUIRE(fa::player::facingFromVector(Vec2{0.2f, 1.0f}, Facing::East) == Facing::South);
    REQUIRE(fa::player::facingFromVector(Vec2{0.0f, -1.0f}, Facing::South) == Facing::North);
    REQUIRE(fa::player::facingFromVector(Vec2{0.0f, 0.0f}, Facing::West) == Facing::West);

    // The sector boundaries sit at 22.5 degrees from each cardinal.
    REQUIRE(fa::player::facingFromVector(Vec2{1.0f, 0.35f}, Facing::South) == Facing::East);
    REQUIRE(fa::player::facingFromVector(Vec2{1.0f, 0.5f}, Facing::South) == Facing::SouthEast);
    REQUIRE(fa::player::facingFromVector(Vec2{0.5f, 1.0f}, Facing::West) == Facing::SouthEast);
    REQUIRE(fa::player::facingFromVector(Vec2{-0.5f, 1.0f}, Facing::East) == Facing::SouthWest);
    REQUIRE(fa::player::facingFromVector(Vec2{-1.0f, 0.5f}, Facing::East) == Facing::SouthWest);
    REQUIRE(fa::player::facingFromVector(Vec2{-1.0f, 0.35f}, Facing::South) == Facing::West);
    REQUIRE(fa::player::facingFromVector(Vec2{-0.5f, -1.0f}, Facing::South) == Facing::NorthWest);
    REQUIRE(fa::player::facingFromVector(Vec2{0.5f, -1.0f}, Facing::South) == Facing::NorthEast);
    REQUIRE(fa::player::facingFromVector(Vec2{0.1f, 0.1f}, Facing::South) == Facing::SouthEast);

    REQUIRE(fa::player::directionVector(Facing::North) == Vec2{0.0f, -1.0f});
    REQUIRE(fa::player::directionVector(Facing::East) == Vec2{1.0f, 0.0f});
    REQUIRE(fa::player::directionVector(Facing::South) == Vec2{0.0f, 1.0f});
    REQUIRE(fa::player::directionVector(Facing::West) == Vec2{-1.0f, 0.0f});
    const Vec2 northEast = fa::player::directionVector(Facing::NorthEast);
    const Vec2 southEast = fa::player::directionVector(Facing::SouthEast);
    const Vec2 southWest = fa::player::directionVector(Facing::SouthWest);
    const Vec2 northWest = fa::player::directionVector(Facing::NorthWest);
    REQUIRE(northEast.x == Approx(0.70710678f));
    REQUIRE(northEast.y == Approx(-0.70710678f));
    REQUIRE(southEast.x == Approx(0.70710678f));
    REQUIRE(southEast.y == Approx(0.70710678f));
    REQUIRE(southWest.x == Approx(-0.70710678f));
    REQUIRE(southWest.y == Approx(0.70710678f));
    REQUIRE(northWest.x == Approx(-0.70710678f));
    REQUIRE(northWest.y == Approx(-0.70710678f));

    REQUIRE(fa::player::isDiagonal(Facing::NorthEast));
    REQUIRE(fa::player::isDiagonal(Facing::SouthWest));
    REQUIRE_FALSE(fa::player::isDiagonal(Facing::North));
    REQUIRE(fa::player::cardinalFacing(Facing::NorthEast) == Facing::North);
    REQUIRE(fa::player::cardinalFacing(Facing::SouthEast) == Facing::South);
    REQUIRE(fa::player::cardinalFacing(Facing::SouthWest) == Facing::South);
    REQUIRE(fa::player::cardinalFacing(Facing::NorthWest) == Facing::North);
    REQUIRE(fa::player::cardinalFacing(Facing::East) == Facing::East);
}

TEST_CASE("Attach adds player components and keeps existing values", "[player][attach]") {
    World world;
    const EntityId id = world.create();
    static_cast<void>(world.add<Transform>(id, Transform{}));
    static_cast<void>(world.add<Velocity>(id, Velocity{}));

    PlayerController::attach(world, id, Stamina{40.0f, 80.0f}, Facing::West);
    REQUIRE(world.has<fa::player::PlayerTag>(id));
    REQUIRE(world.has<FacingDir>(id));
    REQUIRE(world.has<PlayerStateComp>(id));
    REQUIRE(world.has<Stamina>(id));
    REQUIRE(world.has<fa::player::DodgeState>(id));
    REQUIRE(world.tryGet<FacingDir>(id)->value == Facing::West);
    REQUIRE(world.tryGet<Stamina>(id)->current == Approx(40.0f));
    REQUIRE(world.tryGet<Stamina>(id)->max == Approx(80.0f));
    REQUIRE(world.tryGet<PlayerStateComp>(id)->state == PlayerState::Idle);

    PlayerController::attach(world, id, Stamina{10.0f, 10.0f}, Facing::East);
    REQUIRE(world.tryGet<Stamina>(id)->current == Approx(40.0f));
    REQUIRE(world.tryGet<FacingDir>(id)->value == Facing::West);
}

TEST_CASE("Player walks up to walk speed and decelerates to idle", "[player][move]") {
    World world;
    const EntityId id = makePlayer(world);
    PlayerController controller;
    PlayerIntent intent;
    intent.move = Vec2{1.0f, 0.0f};

    step(controller, world, id, intent, 30);
    REQUIRE(velocityOf(world, id).linear.x == Approx(90.0f).margin(0.2f));
    REQUIRE(velocityOf(world, id).linear.y == Approx(0.0f));
    REQUIRE(world.tryGet<PlayerStateComp>(id)->state == PlayerState::Move);
    REQUIRE(world.tryGet<FacingDir>(id)->value == Facing::East);
    REQUIRE(controller.stats().moveSteps == 30);
    REQUIRE(controller.stats().updates == 30);
    REQUIRE(controller.animationParams().moving);
    REQUIRE(controller.animationParams().speed == Approx(90.0f).margin(0.2f));

    intent.move = Vec2{0.0f, 0.0f};
    step(controller, world, id, intent, 10);
    REQUIRE(velocityOf(world, id).linear.x == Approx(0.0f).margin(0.01f));
    REQUIRE(world.tryGet<PlayerStateComp>(id)->state == PlayerState::Idle);
    REQUIRE(controller.stats().idleSteps == 10);
}

TEST_CASE("Diagonal movement is normalized", "[player][move]") {
    World world;
    const EntityId id = makePlayer(world);
    PlayerController controller;
    PlayerIntent intent;
    intent.move = Vec2{1.0f, 1.0f};

    step(controller, world, id, intent, 30);
    const Vec2 velocity = velocityOf(world, id).linear;
    REQUIRE(velocity.x == Approx(63.64f).margin(0.2f));
    REQUIRE(velocity.y == Approx(63.64f).margin(0.2f));
    const float speed = std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y);
    REQUIRE(speed == Approx(90.0f).margin(0.2f));
    REQUIRE(world.tryGet<FacingDir>(id)->value == Facing::SouthEast);
}

TEST_CASE("Run speed applies while run is held", "[player][move]") {
    World world;
    const EntityId id = makePlayer(world);
    PlayerController controller;
    PlayerIntent intent;
    intent.move = Vec2{1.0f, 0.0f};
    intent.run = true;

    step(controller, world, id, intent, 30);
    REQUIRE(velocityOf(world, id).linear.x == Approx(150.0f).margin(0.2f));
}

TEST_CASE("Facing follows eight-way movement and persists when idle", "[player][facing]") {
    World world;
    const EntityId id = makePlayer(world);
    PlayerController controller;
    PlayerIntent intent;

    intent.move = Vec2{0.2f, 1.0f};
    controller.update(world, id, intent, kStep);
    REQUIRE(world.tryGet<FacingDir>(id)->value == Facing::South);

    intent.move = Vec2{-1.0f, 0.1f};
    controller.update(world, id, intent, kStep);
    REQUIRE(world.tryGet<FacingDir>(id)->value == Facing::West);

    intent.move = Vec2{0.0f, -1.0f};
    controller.update(world, id, intent, kStep);
    REQUIRE(world.tryGet<FacingDir>(id)->value == Facing::North);

    intent.move = Vec2{-1.0f, -1.0f};
    controller.update(world, id, intent, kStep);
    REQUIRE(world.tryGet<FacingDir>(id)->value == Facing::NorthWest);

    intent.move = Vec2{0.0f, 0.0f};
    controller.update(world, id, intent, kStep);
    REQUIRE(world.tryGet<FacingDir>(id)->value == Facing::NorthWest);
}

TEST_CASE("Dodge consumes stamina, moves and has an invulnerability window", "[player][dodge]") {
    World world;
    const EntityId id = makePlayer(world);
    PlayerController controller;
    PlayerIntent intent;
    intent.move = Vec2{1.0f, 0.0f};
    intent.dodge = true;

    controller.update(world, id, intent, kStep);
    REQUIRE(world.tryGet<Stamina>(id)->current == Approx(75.0f));
    REQUIRE(PlayerController::isDodging(world, id));
    REQUIRE(world.tryGet<fa::player::DodgeState>(id)->timeLeft > 0.0f);
    REQUIRE(velocityOf(world, id).linear.x == Approx(192.0f).margin(0.2f));
    REQUIRE(controller.events().size() == 1);
    REQUIRE(controller.events()[0].kind == PlayerActionKind::Dodge);
    REQUIRE(controller.events()[0].facing == Facing::East);
    REQUIRE_FALSE(PlayerController::isInvulnerable(world, id, controller.tuning()));

    intent.dodge = false;
    step(controller, world, id, intent, 6);
    REQUIRE(PlayerController::isInvulnerable(world, id, controller.tuning()));
    step(controller, world, id, intent, 6);
    REQUIRE_FALSE(PlayerController::isInvulnerable(world, id, controller.tuning()));

    for (int index = 0; index < 20 && PlayerController::isDodging(world, id); ++index) {
        controller.update(world, id, intent, kStep);
    }
    REQUIRE_FALSE(PlayerController::isDodging(world, id));
    REQUIRE(world.tryGet<fa::player::DodgeState>(id)->cooldown > 0.0f);
}

TEST_CASE("Diagonal dodge keeps the configured speed", "[player][dodge][facing]") {
    World world;
    const EntityId id = makePlayer(world);
    PlayerController controller;
    PlayerIntent intent;
    intent.move = Vec2{1.0f, 1.0f};
    intent.dodge = true;

    controller.update(world, id, intent, kStep);
    REQUIRE(world.tryGet<FacingDir>(id)->value == Facing::SouthEast);
    REQUIRE(controller.events()[0].facing == Facing::SouthEast);

    const Vec2 velocity = velocityOf(world, id).linear;
    const float speed = std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y);
    REQUIRE(speed == Approx(192.0f).margin(0.2f));
    REQUIRE(velocity.x == Approx(135.76f).margin(0.2f));
    REQUIRE(velocity.y == Approx(135.76f).margin(0.2f));
}

TEST_CASE("Dodge rejects missing stamina, no direction, busy and cooldown", "[player][dodge]") {
    World world;
    const EntityId weak = makePlayer(world, Vec2{}, Stamina{10.0f, 100.0f});
    PlayerController controller;
    PlayerIntent intent;
    intent.move = Vec2{1.0f, 0.0f};
    intent.dodge = true;

    controller.update(world, weak, intent, kStep);
    REQUIRE_FALSE(PlayerController::isDodging(world, weak));
    REQUIRE(controller.events().size() == 1);
    REQUIRE(controller.events()[0].kind == PlayerActionKind::ActionRejected);
    REQUIRE(controller.events()[0].errorCode == err::kInsufficientStamina);
    REQUIRE(controller.stats().dodgeRejections == 1);

    const EntityId noDirection = makePlayer(world);
    controller.clearEvents();
    PlayerIntent stationary;
    stationary.dodge = true;
    controller.update(world, noDirection, stationary, kStep);
    REQUIRE(controller.events()[0].errorCode == err::kNoDirection);

    const EntityId busy = makePlayer(world);
    controller.clearEvents();
    controller.update(world, busy, intent, kStep);
    REQUIRE(PlayerController::isDodging(world, busy));
    controller.update(world, busy, intent, kStep);
    REQUIRE(controller.events().back().kind == PlayerActionKind::ActionRejected);
    REQUIRE(controller.events().back().errorCode == err::kBusy);

    controller.clearEvents();
    for (int index = 0; index < 30 && PlayerController::isDodging(world, busy); ++index) {
        controller.update(world, busy, PlayerIntent{}, kStep);
    }
    REQUIRE_FALSE(PlayerController::isDodging(world, busy));
    controller.update(world, busy, intent, kStep);
    REQUIRE(controller.events().back().kind == PlayerActionKind::ActionRejected);
    REQUIRE(controller.events().back().errorCode == err::kCooldown);
}

TEST_CASE("UseTool and Attack lock movement and emit requests", "[player][action]") {
    World world;
    const EntityId id = makePlayer(world);
    PlayerController controller;
    PlayerIntent intent;
    intent.move = Vec2{1.0f, 0.0f};
    intent.useTool = true;
    intent.payload = 42;

    controller.update(world, id, intent, kStep);
    REQUIRE(world.tryGet<PlayerStateComp>(id)->state == PlayerState::UseTool);
    REQUIRE(world.tryGet<PlayerStateComp>(id)->actionLock > 0.0f);
    REQUIRE(velocityOf(world, id).linear.x == Approx(0.0f).margin(0.01f));
    REQUIRE(controller.events().size() == 1);
    REQUIRE(controller.events()[0].kind == PlayerActionKind::UseTool);
    REQUIRE(controller.events()[0].payload == 42u);
    REQUIRE(controller.events()[0].facing == Facing::East);

    intent.useTool = false;
    step(controller, world, id, intent, 3);
    REQUIRE(world.tryGet<PlayerStateComp>(id)->state == PlayerState::UseTool);
    REQUIRE(velocityOf(world, id).linear.x == Approx(0.0f).margin(0.01f));

    controller.clearEvents();
    PlayerIntent attack;
    attack.move = Vec2{1.0f, 0.0f};
    attack.attack = true;
    controller.update(world, id, attack, kStep);
    REQUIRE(controller.events().size() == 1);
    REQUIRE(controller.events()[0].kind == PlayerActionKind::ActionRejected);
    REQUIRE(controller.events()[0].errorCode == err::kBusy);

    step(controller, world, id, intent, 20);
    REQUIRE(world.tryGet<PlayerStateComp>(id)->state == PlayerState::Move);
    REQUIRE(velocityOf(world, id).linear.x > 0.0f);

    controller.clearEvents();
    controller.update(world, id, attack, kStep);
    REQUIRE(controller.events().size() == 1);
    REQUIRE(controller.events()[0].kind == PlayerActionKind::Attack);
}

TEST_CASE("Interact and inventory requests do not lock movement", "[player][action]") {
    World world;
    const EntityId id = makePlayer(world);
    PlayerController controller;
    PlayerIntent intent;
    intent.move = Vec2{1.0f, 0.0f};
    intent.interact = true;
    intent.openInventory = true;
    intent.payload = 7;

    controller.update(world, id, intent, kStep);
    REQUIRE(controller.events().size() == 2);
    REQUIRE(controller.events()[0].kind == PlayerActionKind::OpenInventory);
    REQUIRE(controller.events()[0].payload == 7u);
    REQUIRE(controller.events()[1].kind == PlayerActionKind::Interact);
    REQUIRE(world.tryGet<PlayerStateComp>(id)->state == PlayerState::Move);
    REQUIRE(world.tryGet<PlayerStateComp>(id)->actionLock == Approx(0.0f));
    REQUIRE(velocityOf(world, id).linear.x > 0.0f);
}

TEST_CASE("Dead players ignore input and stop moving", "[player][dead]") {
    World world;
    const EntityId id = makePlayer(world);
    static_cast<void>(world.add<Health>(id, Health{0, 100}));
    PlayerController controller;
    PlayerIntent intent;
    intent.move = Vec2{1.0f, 0.0f};

    controller.update(world, id, intent, kStep);
    REQUIRE(world.tryGet<PlayerStateComp>(id)->state == PlayerState::Dead);
    REQUIRE(velocityOf(world, id).linear.x == Approx(0.0f));
    REQUIRE(controller.stats().moveSteps == 0);
    REQUIRE(controller.events().empty());
}

TEST_CASE("Stamina regenerates while not dodging", "[player][stamina]") {
    World world;
    const EntityId id = makePlayer(world, Vec2{}, Stamina{50.0f, 100.0f});
    PlayerController controller;
    PlayerIntent intent;

    step(controller, world, id, intent, 60);
    REQUIRE(world.tryGet<Stamina>(id)->current == Approx(70.0f).margin(0.1f));

    step(controller, world, id, intent, 120);
    REQUIRE(world.tryGet<Stamina>(id)->current == Approx(100.0f));
}

TEST_CASE("Running drains stamina and falls back to walking at zero", "[player][stamina][run]") {
    World world;
    const EntityId id = makePlayer(world, Vec2{}, Stamina{100.0f, 100.0f});
    PlayerTuning tuning;
    tuning.runStaminaPerSecond = 10.0f;
    tuning.staminaRegenPerSecond = 20.0f;
    PlayerController controller(tuning);
    PlayerIntent intent;
    intent.move = Vec2{1.0f, 0.0f};
    intent.run = true;

    step(controller, world, id, intent, 60);
    REQUIRE(world.tryGet<Stamina>(id)->current == Approx(90.0f).margin(0.2f));
    REQUIRE(velocityOf(world, id).linear.x == Approx(tuning.runSpeed).margin(5.0f));

    step(controller, world, id, intent, 600);
    REQUIRE(world.tryGet<Stamina>(id)->current == Approx(0.0f).margin(0.01f));
    REQUIRE(velocityOf(world, id).linear.x == Approx(tuning.walkSpeed).margin(5.0f));

    intent.run = false;
    step(controller, world, id, intent, 60);
    REQUIRE(world.tryGet<Stamina>(id)->current == Approx(20.0f).margin(0.5f));
}

TEST_CASE("Tool and attack requests consume stamina", "[player][stamina][action]") {
    World world;
    const EntityId id = makePlayer(world, Vec2{}, Stamina{10.0f, 100.0f});
    PlayerTuning tuning;
    tuning.useToolStaminaCost = 3.0f;
    tuning.attackStaminaCost = 4.0f;
    tuning.staminaRegenPerSecond = 0.0f;
    tuning.actionLockDuration = 0.0f;
    PlayerController controller(tuning);

    PlayerIntent use;
    use.useTool = true;
    controller.update(world, id, use, kStep);
    REQUIRE(world.tryGet<Stamina>(id)->current == Approx(7.0f));
    controller.update(world, id, use, kStep);
    REQUIRE(world.tryGet<Stamina>(id)->current == Approx(4.0f));

    PlayerIntent attack;
    attack.attack = true;
    controller.update(world, id, attack, kStep);
    REQUIRE(world.tryGet<Stamina>(id)->current == Approx(0.0f));

    controller.clearEvents();
    controller.update(world, id, attack, kStep);
    REQUIRE(controller.events().size() == 1);
    REQUIRE(controller.events()[0].kind == PlayerActionKind::ActionRejected);
    REQUIRE(controller.events()[0].errorCode == err::kInsufficientStamina);
}

TEST_CASE("PlayerSystem maps input to intent and honours scripted movement", "[player][system]") {
    World world;
    const EntityId id = makePlayer(world);
    FakeInput input;
    input.move = Vec2{1.0f, 0.0f};
    input.down.emplace_back(fa::input::actionIdFromName(fa::player::kActionNameRun), true);
    input.pressed.emplace_back(fa::input::actionIdFromName(fa::player::kActionNameUseTool), true);

    PlayerSystem system(input);
    fa::time::FrameContext context;
    context.fixedDeltaSeconds = kStep;
    fa::scene::CommandBuffer commands;
    system.onFixedUpdate(world, context, commands);

    REQUIRE(velocityOf(world, id).linear.x == Approx(0.0f).margin(0.01f));
    REQUIRE(world.tryGet<PlayerStateComp>(id)->state == PlayerState::UseTool);
    REQUIRE(system.controller().events().size() == 1);
    REQUIRE(system.controller().events()[0].kind == PlayerActionKind::UseTool);

    input.pressed.clear();
    input.move = Vec2{1.0f, 0.0f};
    system.setScriptedMove(Vec2{0.0f, 1.0f});
    for (int index = 0; index < 20; ++index) {
        system.controller().update(world, id, PlayerIntent{}, kStep);
    }
    system.onFixedUpdate(world, context, commands);
    REQUIRE(velocityOf(world, id).linear.y > 0.0f);
    REQUIRE(velocityOf(world, id).linear.x < 0.1f);
}
