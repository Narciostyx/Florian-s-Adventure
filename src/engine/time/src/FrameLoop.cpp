#include "fa/time/FrameLoop.hpp"

#include <vector>

#include <fa/platform/Platform.hpp>

namespace fa::time {
namespace {

constexpr int kExitOk = 0;
constexpr int kExitNoClock = 1;

}  // namespace

FrameLoop::FrameLoop(platform::IPlatform& platform, IFrameSink& sink, FrameLoopConfig config)
    : m_platform(platform), m_sink(sink), m_config(config),
      m_clock(config.fixedStepSeconds, config.maxFixedStepsPerFrame, config.maxRealDeltaSeconds) {}

int FrameLoop::run() {
    const double startSeconds = m_platform.monotonicSeconds();
    if (startSeconds <= 0.0) {
        return kExitNoClock;
    }

    const double frameBudgetSeconds =
        m_config.pacingFramesPerSecond > 0 ? 1.0 / static_cast<double>(m_config.pacingFramesPerSecond) : 0.0;

    std::vector<platform::PlatformEvent> events;
    double previousSeconds = startSeconds;

    while (!m_quitRequested) {
        events.clear();
        const bool stillRunning = m_platform.pumpEvents(events);
        m_sink.onEvents(events);
        if (!stillRunning) {
            m_quitRequested = true;
            break;
        }

        const double nowSeconds = m_platform.monotonicSeconds();
        double realDeltaSeconds = nowSeconds - previousSeconds;
        previousSeconds = nowSeconds;
        if (!(realDeltaSeconds > 0.0)) {
            realDeltaSeconds = 0.0;
        }

        const double gameplayDelta =
            realDeltaSeconds * static_cast<double>(m_time.effectiveScale(TimeDomain::Gameplay));
        const int steps = m_clock.advance(gameplayDelta);

        m_time.beginFrame(realDeltaSeconds,
                          static_cast<std::uint32_t>(steps),
                          m_clock.interpolationAlpha(),
                          m_clock.fixedStepIndex(),
                          m_clock.fixedStepSeconds());
        const FrameContext& context = m_time.context();

        for (int step = 0; step < steps; ++step) {
            m_sink.onFixedUpdate(context);
        }
        m_sink.onVariableUpdate(context);
        m_sink.onRender(context);

        m_time.endFrame();
        ++m_frameCount;

        if (m_config.maxFrames > 0 && m_frameCount >= m_config.maxFrames) {
            break;
        }

        const double frameElapsedSeconds = m_platform.monotonicSeconds() - nowSeconds;
        if (frameBudgetSeconds > 0.0 && frameElapsedSeconds < frameBudgetSeconds) {
            m_platform.sleepSeconds(frameBudgetSeconds - frameElapsedSeconds);
        }
    }

    return kExitOk;
}

void FrameLoop::requestQuit() {
    m_quitRequested = true;
}

const ITimeService& FrameLoop::time() const {
    return m_time;
}

TimeService& FrameLoop::timeService() {
    return m_time;
}

const FixedStepClock& FrameLoop::clock() const {
    return m_clock;
}

std::uint64_t FrameLoop::frameCount() const {
    return m_frameCount;
}

}  // namespace fa::time
