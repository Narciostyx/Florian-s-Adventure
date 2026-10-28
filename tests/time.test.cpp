/// \file
/// \brief Tests for M04: fixed-step accumulation, time domains and the frame loop.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <fa/core/Result.hpp>
#include <fa/platform/Platform.hpp>
#include <fa/time/FixedStepClock.hpp>
#include <fa/time/FrameContext.hpp>
#include <fa/time/FrameLoop.hpp>
#include <fa/time/TimeDomain.hpp>
#include <fa/time/TimeService.hpp>

namespace {

using fa::time::FixedStepClock;
using fa::time::FrameContext;
using fa::time::FrameLoop;
using fa::time::FrameLoopConfig;
using fa::time::IFrameSink;
using fa::time::kFixedStepSeconds;
using fa::time::TimeDomain;
using fa::time::TimeService;

using Catch::Approx;

constexpr double kToleranceSeconds = 1e-9;

/// \brief Minimal IWindow so the fake platform can hand out a reference.
class FakeWindow final : public fa::platform::IWindow {
public:
    void setTitle(std::string_view) override {}
    void resize(int, int) override {}
    void setFullscreen(bool enabled) override { m_fullscreen = enabled; }
    int width() const override { return 1280; }
    int height() const override { return 720; }
    float dpiScale() const override { return 1.0f; }
    bool isFullscreen() const override { return m_fullscreen; }
    void* nativeHandle() const override { return nullptr; }

private:
    bool m_fullscreen = false;
};

/// \brief Minimal IFileSystem; the frame loop never touches it.
class FakeFileSystem final : public fa::platform::IFileSystem {
public:
    bool exists(std::string_view) const override { return false; }
    bool isDirectory(std::string_view) const override { return false; }
    fa::core::Result<std::string> readText(std::string_view) override {
        return fa::core::Result<std::string>::err(2003, "fake");
    }
    fa::core::Result<std::string> readBinary(std::string_view) override {
        return fa::core::Result<std::string>::err(2003, "fake");
    }
    fa::core::Status writeText(std::string_view, std::string_view) override {
        return fa::core::errorStatus(2004, "fake");
    }
    fa::core::Status writeTextAtomic(std::string_view, std::string_view) override {
        return fa::core::errorStatus(2004, "fake");
    }
    fa::core::Status createDirectories(std::string_view) override { return fa::core::okStatus(); }
    fa::core::Result<std::vector<std::string>> listDirectory(std::string_view) override {
        return fa::core::Result<std::vector<std::string>>::err(2006, "fake");
    }
    fa::core::Status remove(std::string_view) override { return fa::core::okStatus(); }
};

/// \brief Deterministic stand-in for the SDL backend, so the loop is testable headlessly.
class FakePlatform final : public fa::platform::IPlatform {
public:
    double nowSeconds = 1.0;
    double frameDeltaSeconds = kFixedStepSeconds;
    int quitAfterFrames = -1;
    int pumpCount = 0;
    double sleptSeconds = 0.0;

    fa::core::Status initialize(const fa::platform::WindowDesc&) override {
        nowSeconds = 1.0;
        return fa::core::okStatus();
    }
    void shutdown() override {}
    bool pumpEvents(std::vector<fa::platform::PlatformEvent>&) override {
        if (quitAfterFrames >= 0 && pumpCount >= quitAfterFrames) {
            return false;
        }
        ++pumpCount;
        nowSeconds += frameDeltaSeconds;
        return true;
    }
    bool quitRequested() const override { return false; }
    fa::platform::IWindow& window() override { return m_window; }
    fa::platform::IFileSystem& files() override { return m_files; }
    double monotonicSeconds() const override { return nowSeconds; }
    void sleepSeconds(double seconds) override {
        sleptSeconds += seconds;
        nowSeconds += seconds;
    }
    fa::core::Status showMessageBox(fa::platform::MessageBoxKind, std::string_view, std::string_view) override {
        return fa::core::okStatus();
    }
    /// The loop never renders, so it does not need a graphics backend.
    fa::platform::IGlContext* glContext() override { return nullptr; }
    /// The loop never plays audio either.
    fa::platform::IAudioDevice* audio() override { return nullptr; }

private:
    FakeWindow m_window;
    FakeFileSystem m_files;
};

/// \brief Records the hook order and the number of steps in each frame.
class CountingSink final : public IFrameSink {
public:
    void onFixedUpdate(const FrameContext&) override {
        ++fixedCalls;
        order.push_back('F');
    }
    void onVariableUpdate(const FrameContext& context) override {
        ++variableCalls;
        lastFrameIndex = context.frameIndex;
        lastRealDelta = context.realDeltaSeconds;
        order.push_back('V');
    }
    void onRender(const FrameContext&) override {
        ++renderCalls;
        order.push_back('R');
    }

    int fixedCalls = 0;
    int variableCalls = 0;
    int renderCalls = 0;
    std::uint64_t lastFrameIndex = 0;
    double lastRealDelta = 0.0;
    std::vector<char> order;
};

}  // namespace

TEST_CASE("FixedStepClock converts a 60 Hz delta into exactly one step", "[time][clock]") {
    FixedStepClock clock;

    REQUIRE(clock.advance(kFixedStepSeconds) == 1);
    REQUIRE(clock.fixedStepIndex() == 1);
    REQUIRE(clock.pendingSeconds() == Approx(0.0).margin(kToleranceSeconds));
    REQUIRE(clock.interpolationAlpha() == Approx(0.0f).margin(1e-6f));
}

TEST_CASE("FixedStepClock runs two steps for a 30 Hz frame", "[time][clock]") {
    FixedStepClock clock;

    REQUIRE(clock.advance(1.0 / 30.0) == 2);
    REQUIRE(clock.fixedStepIndex() == 2);
    REQUIRE(clock.pendingSeconds() == Approx(0.0).margin(kToleranceSeconds));
}

TEST_CASE("FixedStepClock accumulates the same simulated time at 144 Hz", "[time][clock]") {
    FixedStepClock clock;
    constexpr int kFrames = 1440;
    const double delta = 1.0 / 144.0;

    int totalSteps = 0;
    for (int frame = 0; frame < kFrames; ++frame) {
        totalSteps += clock.advance(delta);
    }

    /// Ten seconds of real time must yield ten seconds of simulation, with at most
    /// one step of rounding error left over: nothing accumulates over a long run.
    constexpr double kExpectedSeconds = 10.0;
    REQUIRE(totalSteps >= 599);
    REQUIRE(totalSteps <= 600);
    REQUIRE(clock.simulatedSeconds() == Approx(kExpectedSeconds).margin(kFixedStepSeconds));
    REQUIRE(clock.droppedSteps() == 0);
}

TEST_CASE("FixedStepClock keeps interpolation alpha strictly below one", "[time][clock]") {
    FixedStepClock clock;

    for (int frame = 0; frame < 100; ++frame) {
        clock.advance(kFixedStepSeconds * 0.5);
        REQUIRE(clock.interpolationAlpha() >= 0.0f);
        REQUIRE(clock.interpolationAlpha() < 1.0f);
    }
    /// An even number of half steps leaves no pending time, so nothing is left over.
    REQUIRE(clock.interpolationAlpha() == Approx(0.0f).margin(1e-5f));
}

TEST_CASE("FixedStepClock reports the fraction of a pending step", "[time][clock]") {
    FixedStepClock clock;

    REQUIRE(clock.advance(kFixedStepSeconds * 1.5) == 1);
    REQUIRE(clock.pendingSeconds() == Approx(kFixedStepSeconds * 0.5).margin(kToleranceSeconds));
    REQUIRE(clock.interpolationAlpha() == Approx(0.5f).margin(1e-5f));
}

TEST_CASE("FixedStepClock clamps a long stall and counts the dropped steps", "[time][clock]") {
    FixedStepClock clock;

    /// A one second stall is clamped to maxRealDeltaSeconds before accumulating.
    const int steps = clock.advance(1.0);

    REQUIRE(steps == clock.maxStepsPerFrame());
    REQUIRE(clock.fixedStepIndex() == static_cast<std::uint64_t>(clock.maxStepsPerFrame()));
    REQUIRE(clock.lastDeltaSeconds() == Approx(clock.maxRealDeltaSeconds()));
    REQUIRE(clock.droppedSteps() >= 9);
    REQUIRE(clock.droppedSteps() <= 10);
    REQUIRE(clock.pendingSeconds() < clock.fixedStepSeconds());
}

TEST_CASE("FixedStepClock ignores degenerate deltas", "[time][clock]") {
    FixedStepClock clock;

    REQUIRE(clock.advance(0.0) == 0);
    REQUIRE(clock.advance(-1.0) == 0);
    REQUIRE(clock.advance(std::numeric_limits<double>::quiet_NaN()) == 0);
    REQUIRE(clock.advance(std::numeric_limits<double>::infinity()) == 0);
    REQUIRE(clock.fixedStepIndex() == 0);
    REQUIRE(clock.lastDeltaSeconds() == 0.0);
}

TEST_CASE("FixedStepClock reset discards pending time and counters", "[time][clock]") {
    FixedStepClock clock;
    clock.advance(0.2);

    clock.reset();

    REQUIRE(clock.fixedStepIndex() == 0);
    REQUIRE(clock.droppedSteps() == 0);
    REQUIRE(clock.pendingSeconds() == 0.0);
    REQUIRE(clock.simulatedSeconds() == 0.0);
}

TEST_CASE("TimeService starts every domain unpaused at scale one", "[time][timeservice]") {
    TimeService time;

    for (const TimeDomain domain : {TimeDomain::Gameplay, TimeDomain::Ui, TimeDomain::Cinematic}) {
        REQUIRE_FALSE(time.isPaused(domain));
        REQUIRE(time.scale(domain) == Approx(1.0f));
        REQUIRE(time.effectiveScale(domain) == Approx(1.0f));
    }
}

TEST_CASE("TimeService pause zeroes only the paused domain", "[time][timeservice]") {
    TimeService time;
    time.beginFrame(0.5, 0, 0.0f, 0, kFixedStepSeconds);

    time.setPaused(TimeDomain::Gameplay, true);

    REQUIRE(time.effectiveScale(TimeDomain::Gameplay) == Approx(0.0f));
    REQUIRE(time.deltaFor(TimeDomain::Gameplay) == Approx(0.0));
    REQUIRE(time.deltaFor(TimeDomain::Ui) == Approx(0.5));

    /// Resuming restores the configured scale rather than resetting it.
    time.setScale(TimeDomain::Gameplay, 0.5f);
    time.setPaused(TimeDomain::Gameplay, false);
    REQUIRE(time.scale(TimeDomain::Gameplay) == Approx(0.5f));
    REQUIRE(time.deltaFor(TimeDomain::Gameplay) == Approx(0.25));
}

TEST_CASE("TimeService clamps scale and ignores non-finite values", "[time][timeservice]") {
    TimeService time;

    time.setScale(TimeDomain::Gameplay, -3.0f);
    REQUIRE(time.scale(TimeDomain::Gameplay) == Approx(0.0f));

    time.setScale(TimeDomain::Gameplay, 1000.0f);
    REQUIRE(time.scale(TimeDomain::Gameplay) == Approx(fa::time::kMaxTimeScale));

    time.setScale(TimeDomain::Gameplay, 2.0f);
    time.setScale(TimeDomain::Gameplay, std::numeric_limits<float>::quiet_NaN());
    REQUIRE(time.scale(TimeDomain::Gameplay) == Approx(2.0f));
}

TEST_CASE("TimeService publishes the frame snapshot", "[time][timeservice]") {
    TimeService time;
    time.setScale(TimeDomain::Gameplay, 0.5f);

    time.beginFrame(0.25, 3, 0.75f, 42, kFixedStepSeconds);

    const FrameContext& context = time.context();
    REQUIRE(context.frameIndex == 1);
    REQUIRE(context.realDeltaSeconds == Approx(0.25));
    REQUIRE(context.scaledDeltaSeconds == Approx(0.125));
    REQUIRE(context.fixedStepsThisFrame == 3);
    REQUIRE(context.interpolationAlpha == Approx(0.75f));
    REQUIRE(context.fixedStepIndex == 42);
    REQUIRE(context.gameplayScale == Approx(0.5f));
    REQUIRE_FALSE(context.isPaused);
}

TEST_CASE("TimeService marks the snapshot as paused for the gameplay domain", "[time][timeservice]") {
    TimeService time;
    time.setPaused(TimeDomain::Gameplay, true);

    time.beginFrame(0.016, 0, 0.0f, 10, kFixedStepSeconds);

    REQUIRE(time.context().isPaused);
    REQUIRE(time.context().gameplayScale == Approx(0.0f));
    REQUIRE(time.context().scaledDeltaSeconds == Approx(0.0));
}

TEST_CASE("FrameLoop calls the hooks in fixed, variable, render order", "[time][loop]") {
    FakePlatform platform;
    /// Not a whole multiple of the step, so the frame count does not sit on a
    /// floating-point boundary and each frame runs exactly one fixed step.
    platform.frameDeltaSeconds = kFixedStepSeconds * 1.25;
    CountingSink sink;
    FrameLoopConfig config;
    config.maxFrames = 3;
    FrameLoop loop(platform, sink, config);

    REQUIRE(loop.run() == 0);

    REQUIRE(loop.frameCount() == 3);
    REQUIRE(sink.variableCalls == 3);
    REQUIRE(sink.renderCalls == 3);
    REQUIRE(sink.fixedCalls == 3);
    REQUIRE(std::string(sink.order.begin(), sink.order.end()) == "FVRFVRFVR");
    REQUIRE(sink.lastFrameIndex == 3);
}

TEST_CASE("FrameLoop runs several fixed steps for a slow frame", "[time][loop]") {
    FakePlatform platform;
    platform.frameDeltaSeconds = kFixedStepSeconds * 2.5;
    CountingSink sink;
    FrameLoopConfig config;
    config.maxFrames = 4;
    FrameLoop loop(platform, sink, config);

    REQUIRE(loop.run() == 0);

    /// 2.5 steps per frame alternates two and three steps, so four frames produce
    /// ten steps in total.
    REQUIRE(sink.fixedCalls == 10);
    REQUIRE(loop.clock().fixedStepIndex() == 10);
    REQUIRE(loop.clock().droppedSteps() == 0);
}

TEST_CASE("FrameLoop stops when the platform reports a quit request", "[time][loop]") {
    FakePlatform platform;
    platform.quitAfterFrames = 2;
    CountingSink sink;
    FrameLoop loop(platform, sink);

    REQUIRE(loop.run() == 0);

    REQUIRE(loop.frameCount() == 2);
    REQUIRE(sink.renderCalls == 2);
}

TEST_CASE("FrameLoop keeps rendering while gameplay is paused", "[time][loop]") {
    FakePlatform platform;
    platform.frameDeltaSeconds = 1.0 / 30.0;
    CountingSink sink;
    FrameLoopConfig config;
    config.maxFrames = 4;
    FrameLoop loop(platform, sink, config);
    loop.timeService().setPaused(TimeDomain::Gameplay, true);

    REQUIRE(loop.run() == 0);

    REQUIRE(sink.fixedCalls == 0);
    REQUIRE(sink.variableCalls == 4);
    REQUIRE(sink.renderCalls == 4);
    REQUIRE(loop.time().context().isPaused);
}

TEST_CASE("FrameLoop reports a failure when the platform clock is unavailable", "[time][loop]") {
    FakePlatform platform;
    platform.nowSeconds = 0.0;
    CountingSink sink;
    FrameLoop loop(platform, sink);

    REQUIRE(loop.run() != 0);
    REQUIRE(sink.renderCalls == 0);
}

TEST_CASE("FrameLoop paces frames when a target rate is configured", "[time][loop]") {
    FakePlatform platform;
    platform.frameDeltaSeconds = 0.0;
    CountingSink sink;
    FrameLoopConfig config;
    config.maxFrames = 3;
    config.pacingFramesPerSecond = 60;
    FrameLoop loop(platform, sink, config);

    REQUIRE(loop.run() == 0);

    REQUIRE(sink.renderCalls == 3);
    REQUIRE(platform.sleptSeconds > 0.0);
}

TEST_CASE("TimeDomain has a stable name for logging", "[time][domain]") {
    REQUIRE(fa::time::toString(TimeDomain::Gameplay) == "gameplay");
    REQUIRE(fa::time::toString(TimeDomain::Ui) == "ui");
    REQUIRE(fa::time::toString(TimeDomain::Cinematic) == "cinematic");
    REQUIRE(fa::time::toString(TimeDomain::Count) == "unknown");
}
