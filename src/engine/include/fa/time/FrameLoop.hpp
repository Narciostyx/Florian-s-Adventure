#pragma once

#include <cstdint>
#include <span>

#include <fa/platform/PlatformEvent.hpp>
#include <fa/time/FixedStepClock.hpp>
#include <fa/time/FrameContext.hpp>
#include <fa/time/TimeService.hpp>

namespace fa::platform {
class IPlatform;
}

namespace fa::time {

/// \brief Receives the per-frame callbacks.
///
/// One listener per loop keeps ordering explicit; a system that needs a specific
/// position in the order is composed inside the listener. All hooks have empty
/// defaults so a listener only overrides what it actually does.
class IFrameSink {
public:
    virtual ~IFrameSink() = default;

    /// \brief Receives the platform events pumped for this frame.
    ///
    /// A pass-through so the loop's own event pump does not swallow input; the
    /// input layer (M07) becomes the real owner of these events. Called once per
    /// frame, before onFixedUpdate(), and the span is empty when nothing arrived.
    virtual void onEvents(std::span<const platform::PlatformEvent> events) { (void)events; }

    /// \brief Runs gameplay logic once per fixed step.
    ///
    /// Called 0..config.maxFixedStepsPerFrame times per frame, always before
    /// onVariableUpdate(). \p context holds values for the whole frame; the
    /// authoritative tick count is context.fixedStepIndex at the end of the group.
    virtual void onFixedUpdate(const FrameContext& context) { (void)context; }

    /// \brief Runs per displayed frame: input mapping, UI, animation, anything
    /// that should look smooth rather than step at the simulation rate.
    virtual void onVariableUpdate(const FrameContext& context) { (void)context; }

    /// \brief Submits the frame, using context.interpolationAlpha to smooth the gap
    /// between the last two fixed states.
    virtual void onRender(const FrameContext& context) { (void)context; }
};

/// \brief Frame loop configuration. Defaults match the design documents.
struct FrameLoopConfig {
    /// \brief Length of one simulation step.
    double fixedStepSeconds = kFixedStepSeconds;

    /// \brief Upper bound on fixed steps per frame.
    int maxFixedStepsPerFrame = kMaxCatchUpSteps;

    /// \brief Wall-clock delta clamp, so a stall is never replayed.
    double maxRealDeltaSeconds = kMaxRealDeltaSeconds;

    /// \brief Frames per second to pace to, or 0 for no pacing.
    ///
    /// Only for headless runs and diagnostics. Real frame pacing belongs to
    /// vertical sync in the renderer (M06); a software cap here would fight it.
    int pacingFramesPerSecond = 0;

    /// \brief Stops after this many frames, or 0 to run until the user quits.
    std::uint64_t maxFrames = 0;
};

/// \brief A driven main loop, for callers that need to keep it behind an interface.
class IFrameLoop {
public:
    virtual ~IFrameLoop() = default;

    /// \brief Runs until a quit request or config.maxFrames is reached.
    ///
    /// \return 0 on a normal exit; non-zero if the platform clock is unavailable,
    /// which means frame timing cannot be measured at all.
    virtual int run() = 0;

    /// \brief Asks the loop to stop after the current frame.
    ///
    /// Safe to call from a callback on the loop's own thread.
    virtual void requestQuit() = 0;

    /// \brief The time service the loop drives, for menus and debug overlays.
    virtual const ITimeService& time() const = 0;
};

/// \brief The single main loop: event pump, fixed steps, variable update, render.
///
/// Order per frame is fixed and not configurable:
/// pump events -> onEvents -> advance clock -> onFixedUpdate x N -> onVariableUpdate
/// -> onRender. Pausing happens through the time service, not by skipping the loop,
/// so UI and render keep running while the simulation is frozen.
class FrameLoop final : public IFrameLoop {
public:
    /// \param platform must outlive the loop; it supplies events, the clock and sleep.
    /// \param sink must outlive the loop; it receives the callbacks.
    FrameLoop(platform::IPlatform& platform, IFrameSink& sink, FrameLoopConfig config = {});

    int run() override;
    void requestQuit() override;

    const ITimeService& time() const override;

    /// \brief Mutable access for setup code (pausing a domain before the first frame).
    TimeService& timeService();

    /// \brief The clock driving fixed steps, for frame statistics and debug UI.
    const FixedStepClock& clock() const;

    /// \brief Frames completed so far.
    std::uint64_t frameCount() const;

private:
    platform::IPlatform& m_platform;
    IFrameSink& m_sink;
    FrameLoopConfig m_config;
    FixedStepClock m_clock;
    TimeService m_time;
    std::uint64_t m_frameCount = 0;
    bool m_quitRequested = false;
};

}  // namespace fa::time
