/// \file
/// \brief Tests for M10: clip validation, direction selection, frame advance,
/// looping, events, pause/speed, and the state machine's transitions and
/// directional clips. Everything is headless; no renderer is needed.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <fa/anim/AnimationClip.hpp>
#include <fa/anim/AnimationService.hpp>
#include <fa/anim/AnimationStateMachine.hpp>
#include <fa/anim/Animator.hpp>
#include <fa/core/Math.hpp>
#include <fa/render/RenderTypes.hpp>

namespace {

using fa::anim::AnimationClip;
using fa::anim::AnimationFrame;
using fa::anim::AnimationParams;
using fa::anim::AnimationService;
using fa::anim::AnimationStateMachine;
using fa::anim::AnimationTransition;
using fa::anim::Animator;
using fa::anim::Direction;
using fa::anim::DirectionalClips;
using fa::anim::directionFromVector;

std::shared_ptr<AnimationClip> makeClip(const std::vector<float>& durations, bool loop, bool withEvents = false) {
    auto clip = std::make_shared<AnimationClip>();
    clip->texture = fa::render::TextureHandle{1};
    clip->loop = loop;
    for (std::size_t index = 0; index < durations.size(); ++index) {
        AnimationFrame frame;
        frame.source = fa::core::makeRect(static_cast<float>(index) * 8.0f, 0.0f, 8.0f, 8.0f);
        frame.duration = durations[index];
        if (withEvents) {
            frame.event = "e" + std::to_string(index);
        }
        clip->frames.push_back(frame);
    }
    return clip;
}

}  // namespace

TEST_CASE("AnimationClip validates and selects directions", "[anim]") {
    AnimationClip empty;
    CHECK_FALSE(empty.isValid());
    CHECK(empty.totalDuration() == 0.0f);

    AnimationClip invalid;
    invalid.texture = fa::render::TextureHandle{1};
    invalid.frames.push_back(AnimationFrame{fa::core::makeRect(0, 0, 8, 8), 0.0f, {}});
    CHECK_FALSE(invalid.isValid());

    const auto clip = makeClip({0.1f, 0.2f, 0.3f}, true);
    CHECK(clip->isValid());
    CHECK(clip->totalDuration() == Catch::Approx(0.6f));

    CHECK(directionFromVector(fa::core::Vec2{0.0f, 0.0f}) == Direction::Down);
    CHECK(directionFromVector(fa::core::Vec2{1.0f, 0.0f}) == Direction::Right);
    CHECK(directionFromVector(fa::core::Vec2{-1.0f, 0.0f}) == Direction::Left);
    CHECK(directionFromVector(fa::core::Vec2{0.0f, -1.0f}) == Direction::Up);
    CHECK(directionFromVector(fa::core::Vec2{0.0f, 1.0f}) == Direction::Down);
    CHECK(directionFromVector(fa::core::Vec2{0.4f, 0.6f}) == Direction::Down);
    CHECK(directionFromVector(fa::core::Vec2{0.6f, 0.4f}) == Direction::Right);
    CHECK(directionFromVector(fa::core::Vec2{0.0f, 0.0f}, Direction::Left) == Direction::Left);
}

TEST_CASE("AnimationService advances frames and emits events", "[anim]") {
    AnimationService service;
    Animator animator;
    const auto clip = makeClip({0.1f, 0.1f, 0.1f}, false, true);
    service.play(animator, clip, true);
    REQUIRE(animator.clip == clip);
    CHECK(animator.playing);
    CHECK(animator.frameIndex == 0);

    service.update(animator, 0.0f);
    REQUIRE(animator.events.size() == 1);
    CHECK(animator.events[0].name == "e0");
    CHECK(animator.frameIndex == 0);

    service.update(animator, 0.1f);
    REQUIRE(animator.events.size() == 1);
    CHECK(animator.events[0].name == "e1");
    CHECK(animator.frameIndex == 1);

    service.update(animator, 0.15f);
    REQUIRE(animator.events.size() == 1);
    CHECK(animator.events[0].name == "e2");
    CHECK(animator.frameIndex == 2);

    service.update(animator, 0.1f);
    CHECK(animator.finished);
    CHECK_FALSE(animator.playing);
    CHECK(animator.frameIndex == 2);
    CHECK(service.currentFrame(animator) != nullptr);
    CHECK(service.currentFrame(animator)->event == "e2");
}

TEST_CASE("AnimationService loops, pauses and applies speed", "[anim]") {
    AnimationService service;
    Animator animator;
    const auto clip = makeClip({0.1f, 0.1f}, true, true);
    service.play(animator, clip, true);

    service.update(animator, 0.0f);
    CHECK(animator.events.size() == 1);
    CHECK(animator.events[0].name == "e0");

    service.update(animator, 0.25f);
    CHECK(animator.frameIndex == 0);
    CHECK(animator.time == Catch::Approx(0.05f));
    REQUIRE(animator.events.size() == 2);
    CHECK(animator.events[0].name == "e1");
    CHECK(animator.events[1].name == "e0");

    animator.speed = 2.0f;
    const std::size_t before = animator.frameIndex;
    service.update(animator, 0.05f);
    CHECK(animator.frameIndex != before);

    animator.speed = 0.0f;
    const std::size_t pausedFrame = animator.frameIndex;
    service.update(animator, 1.0f);
    CHECK(animator.frameIndex == pausedFrame);

    service.stop(animator);
    CHECK_FALSE(animator.playing);
    CHECK(animator.events.empty());
}

TEST_CASE("AnimationStateMachine transitions and direction variants", "[anim]") {
    const auto idle = makeClip({0.1f}, true, true);
    const auto walkDown = makeClip({0.1f}, true, true);
    const auto walkUp = makeClip({0.1f}, true, true);

    DirectionalClips idleClips;
    idleClips.down = idle;
    DirectionalClips walkClips;
    walkClips.down = walkDown;
    walkClips.up = walkUp;

    AnimationStateMachine machine;
    REQUIRE(machine.addState("idle", idleClips));
    REQUIRE(machine.addState("walk", walkClips));
    CHECK_FALSE(machine.addState("idle", idleClips));
    CHECK_FALSE(machine.addState("", idleClips));

    REQUIRE(machine.addTransition(
        AnimationTransition{"idle", "walk", [](const AnimationParams& params) { return params.speed > 0.0f; }, 0}));
    REQUIRE(machine.addTransition(
        AnimationTransition{"walk", "idle", [](const AnimationParams& params) { return params.speed <= 0.0f; }, 0}));
    CHECK_FALSE(machine.addTransition(AnimationTransition{"idle", "", {}, 0}));

    Animator animator;
    AnimationParams params;
    params.speed = 0.0f;
    machine.update(animator, params, 0.0f);
    CHECK(machine.state() == "idle");
    CHECK(animator.clip == idle);

    params.speed = 10.0f;
    params.direction = Direction::Up;
    machine.update(animator, params, 0.0f);
    CHECK(machine.state() == "walk");
    CHECK(animator.clip == walkUp);

    params.direction = Direction::Left;
    machine.update(animator, params, 0.0f);
    CHECK(animator.clip == walkDown);

    params.speed = 0.0f;
    machine.update(animator, params, 0.0f);
    CHECK(machine.state() == "idle");
    CHECK(animator.clip == idle);

    CHECK(machine.setState("walk"));
    CHECK(machine.state() == "walk");
    CHECK_FALSE(machine.setState("missing"));

    DirectionalClips fallback;
    fallback.up = walkUp;
    CHECK(fallback.forDirection(Direction::Right) == walkUp);
}
