/// \file
/// \brief Composition root: the only translation unit that knows about every engine module.
///
/// Wires M02 (platform) and M04 (frame loop) so window, event and timing behave
/// end to end. M06 (render) is attached here later.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <fa/core/Result.hpp>
#include <fa/core/Version.hpp>
#include <fa/events/EventBus.hpp>
#include <fa/events/JobSystem.hpp>
#include <fa/input/Input.hpp>
#include <fa/platform/Platform.hpp>
#include <fa/time/FrameContext.hpp>
#include <fa/time/FrameLoop.hpp>

namespace {

struct Options {
    /// A visible window is opt-in so CI and automated runs never block.
    bool visibleWindow = false;
    /// 0 means "run until the user closes the window".
    int frames = 0;
};

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--window") {
            options.visibleWindow = true;
        } else if (arg == "--frames" && i + 1 < argc) {
            options.frames = std::atoi(argv[++i]);
        }
    }
    return options;
}

void printBanner() {
    const std::string_view version = fa::core::engineVersion();
    std::printf("Florian's Adventure - engine v%.*s\n", static_cast<int>(version.size()), version.data());
}

/// \brief Published once per fixed simulation tick.
struct TickCompleted {
    std::uint64_t tick = 0;
};

/// \brief Published from a job completion, so it reaches handlers on the main thread.
struct UploadFinished {
    int value = 0;
};

/// \brief Minimal end-to-end use of M03 inside the smoke run.
///
/// Covers all three delivery paths the engine relies on: a synchronous publish from
/// the fixed update, work handed to the job pool, and a completion callback that
/// enqueues an event delivered by the next drain on the main thread.
class EventsAndJobsDemo final {
public:
    EventsAndJobsDemo() {
        m_tickSubscription = fa::events::Subscription(m_bus, m_bus.subscribe<TickCompleted>([this](const TickCompleted& event) {
                                  lastTick = event.tick;
                                  ++tickEvents;
                              }));
        m_uploadSubscription = fa::events::Subscription(m_bus, m_bus.subscribe<UploadFinished>([this](const UploadFinished& event) {
                                    uploadedValue += event.value;
                                    ++uploadEvents;
                                }));
    }

    void beginTick(std::uint64_t tick) {
        m_bus.publish(TickCompleted{tick});
        const fa::events::JobHandle handle =
            m_jobs.submit(fa::events::JobDesc{"smoke-upload", []() { return fa::core::okStatus(); }});
        m_inFlight.push_back(handle);
        m_jobs.onComplete(handle, [this, tick](fa::core::Status status) {
            if (status.isOk()) {
                m_bus.enqueue(UploadFinished{static_cast<int>(tick)});
            }
        });
    }

    void endFrame() {
        m_jobs.drainCompletedJobs();
        m_bus.dispatchQueued();
    }

    /// \brief Blocks for every submitted job so the run ends with every event delivered.
    /// \return False when any of those jobs reported an error.
    bool flush() {
        bool ok = true;
        for (const fa::events::JobHandle& handle : m_inFlight) {
            ok = m_jobs.wait(handle).isOk() && ok;
        }
        m_inFlight.clear();
        endFrame();
        return ok;
    }

    std::size_t tickEvents = 0;
    std::size_t uploadEvents = 0;
    std::uint64_t lastTick = 0;
    int uploadedValue = 0;

private:
    fa::events::EventBus m_bus;
    fa::events::JobSystem m_jobs{2};
    std::vector<fa::events::JobHandle> m_inFlight;
    fa::events::Subscription m_tickSubscription;
    fa::events::Subscription m_uploadSubscription;
};

/// \brief Temporary sink: counts events and reports the frame timing it observes.
///
/// Stands in for the layers that will own this work later (M03 events, M06 render)
/// so the loop itself can be verified from the app. It also owns the M07 input system
/// and drives it in the order the module contract requires:
/// `poll(events)` in onEvents, read actions during the updates, `endFrame()` in onRender.
class SmokeSink final : public fa::time::IFrameSink {
public:
    SmokeSink() {
        input.addDevice(std::make_unique<fa::input::KeyboardDevice>());
        input.addDevice(std::make_unique<fa::input::MouseDevice>());
        input.applyDefaultBindings();
    }

    void onEvents(std::span<const fa::platform::PlatformEvent> events) override {
        input.poll(events);
        forwardedEvents += events.size();
        for (const fa::platform::PlatformEvent& event : events) {
            switch (event.type) {
                case fa::platform::PlatformEventType::Quit:
                    ++quitEvents;
                    break;
                case fa::platform::PlatformEventType::WindowResized:
                    ++resizeEvents;
                    std::printf("resized -> %dx%d\n", event.width, event.height);
                    break;
                default:
                    break;
            }
        }
    }

    void onFixedUpdate(const fa::time::FrameContext& context) override {
        ++fixedSteps;
        lastTick = context.fixedStepIndex;
        const fa::core::Vec2 axis = input.moveAxis();
        lastMoveAxis = axis;
        if (axis.x != 0.0f || axis.y != 0.0f) {
            ++movingSteps;
        }
        if (demo != nullptr) {
            demo->beginTick(context.fixedStepIndex);
        }
    }

    void onVariableUpdate(const fa::time::FrameContext& context) override {
        lastRealDelta = context.realDeltaSeconds;
    }

    void onRender(const fa::time::FrameContext&) override {
        ++renderedFrames;
        lastMouse = input.mousePosition();
        if (demo != nullptr) {
            demo->endFrame();
        }
        input.endFrame();
    }

    /// Declared before `demo` so the input system outlives nothing that needs it and
    /// construction order stays obvious.
    fa::input::InputSystem input;
    EventsAndJobsDemo* demo = nullptr;
    int quitEvents = 0;
    int resizeEvents = 0;
    int renderedFrames = 0;
    std::uint64_t forwardedEvents = 0;
    std::uint64_t movingSteps = 0;
    std::uint64_t fixedSteps = 0;
    std::uint64_t lastTick = 0;
    fa::core::Vec2 lastMoveAxis{};
    fa::core::Vec2 lastMouse{};
    double lastRealDelta = 0.0;
};

}  // namespace

int main(int argc, char** argv) {
    const Options options = parseOptions(argc, argv);
    printBanner();

    std::unique_ptr<fa::platform::IPlatform> platform = fa::platform::createPlatform();

    fa::platform::WindowDesc desc;
    desc.title = "Florian's Adventure";
    desc.width = 1280;
    desc.height = 720;
    desc.hidden = !options.visibleWindow;

    const fa::core::Status initialized = platform->initialize(desc);
    if (initialized.isError()) {
        std::printf("platform init failed (%d): %s\n", initialized.error().code, initialized.error().message.c_str());
        return 1;
    }

    const fa::platform::IWindow& window = platform->window();
    std::printf("window %dx%d px, dpi scale %.2f\n", window.width(), window.height(),
                static_cast<double>(window.dpiScale()));

    EventsAndJobsDemo demo;

    SmokeSink sink;
    sink.demo = &demo;
    fa::time::FrameLoopConfig config;
    /// A hidden smoke run needs a few frames; a visible window runs until closed.
    config.maxFrames = options.frames > 0 ? static_cast<std::uint64_t>(options.frames)
                                         : (options.visibleWindow ? 0u : 3u);
    /// Software pacing only for headless runs; a real window is paced by vsync (M06).
    config.pacingFramesPerSecond = options.visibleWindow ? 0 : fa::time::kFixedStepsPerSecond;

    fa::time::FrameLoop loop(*platform, sink, config);
    const double startedSeconds = platform->monotonicSeconds();
    const int exitCode = loop.run();
    const double elapsedSeconds = platform->monotonicSeconds() - startedSeconds;

    const fa::time::FixedStepClock& clock = loop.clock();
    const bool eventsJobsOk = demo.flush();
    platform->shutdown();

    if (exitCode != 0) {
        std::printf("frame loop failed (%d): the platform clock is unavailable\n", exitCode);
        return exitCode;
    }

    if (!eventsJobsOk) {
        std::printf("events/jobs smoke failed: the final job reported an error\n");
        return 1;
    }

    std::printf("ran %llu frame(s) in %.3fs\n", static_cast<unsigned long long>(loop.frameCount()), elapsedSeconds);
    std::printf("  fixed steps: %llu (tick %llu, step %.4fs), dropped: %llu\n",
                static_cast<unsigned long long>(sink.fixedSteps), static_cast<unsigned long long>(sink.lastTick),
                clock.fixedStepSeconds(), static_cast<unsigned long long>(clock.droppedSteps()));
    std::printf("  simulated: %.3fs, last real delta: %.4fs\n", clock.simulatedSeconds(), sink.lastRealDelta);
    if (elapsedSeconds > 0.0) {
        std::printf("  average: %.1f fps\n", static_cast<double>(sink.renderedFrames) / elapsedSeconds);
    }
    std::printf("  events: quit %d, resize %d\n", sink.quitEvents, sink.resizeEvents);
    std::printf("  m03: %llu tick event(s) (last tick %llu), %llu upload event(s) worth %d\n",
                static_cast<unsigned long long>(demo.tickEvents), static_cast<unsigned long long>(demo.lastTick),
                static_cast<unsigned long long>(demo.uploadEvents), demo.uploadedValue);
    const std::string_view contextName = fa::input::toString(sink.input.activeContext());
    std::printf("  m07: %llu event(s) forwarded, context %.*s, moving %llu step(s), "
                "axis (%.2f, %.2f), mouse (%g, %g)\n",
                static_cast<unsigned long long>(sink.forwardedEvents), static_cast<int>(contextName.size()),
                contextName.data(), static_cast<unsigned long long>(sink.movingSteps),
                static_cast<double>(sink.lastMoveAxis.x), static_cast<double>(sink.lastMoveAxis.y),
                static_cast<double>(sink.lastMouse.x), static_cast<double>(sink.lastMouse.y));
    std::printf("M02+M03+M04+M07 smoke test OK\n");
    return 0;
}
