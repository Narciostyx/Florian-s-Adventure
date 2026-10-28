#pragma once

#include <cstdint>

#include <fa/time/FrameContext.hpp>
#include <fa/time/TimeDomain.hpp>

namespace fa::time {

/// \brief Pause and scale state per TimeDomain, plus the current frame's snapshot.
///
/// Consumers depend on this interface rather than on the frame loop, so a paused
/// scene can still ask for time without knowing how frames are produced.
class ITimeService {
public:
    virtual ~ITimeService() = default;

    /// \brief Pauses or resumes \p domain.
    ///
    /// Pausing is independent of scale: resuming restores the scale that was
    /// configured before the pause.
    virtual void setPaused(TimeDomain domain, bool paused) = 0;

    /// \brief Whether \p domain is paused.
    virtual bool isPaused(TimeDomain domain) const = 0;

    /// \brief Sets the speed multiplier of \p domain.
    ///
    /// Clamped into `[0, kMaxTimeScale]`; a non-finite value is ignored.
    virtual void setScale(TimeDomain domain, float scale) = 0;

    /// \brief The configured scale of \p domain, ignoring pause.
    virtual float scale(TimeDomain domain) const = 0;

    /// \brief The scale actually in effect, i.e. 0 while paused.
    virtual float effectiveScale(TimeDomain domain) const = 0;

    /// \brief Delta a system in \p domain should advance by, in `[0, ±]` seconds.
    ///
    /// Returns `realDeltaSeconds * effectiveScale(domain)`, so paused domains
    /// always see 0 and callers need no extra branch.
    virtual double deltaFor(TimeDomain domain) const = 0;

    /// \brief The snapshot for the current frame.
    ///
    /// Valid only between the frame loop's begin/end of a frame; treat it as
    /// read-only and do not store the reference.
    virtual const FrameContext& context() const = 0;
};

/// \brief Default implementation used by the frame loop.
class TimeService final : public ITimeService {
public:
    TimeService() noexcept;

    void setPaused(TimeDomain domain, bool paused) override;
    bool isPaused(TimeDomain domain) const override;
    void setScale(TimeDomain domain, float scale) override;
    float scale(TimeDomain domain) const override;
    float effectiveScale(TimeDomain domain) const override;
    double deltaFor(TimeDomain domain) const override;
    const FrameContext& context() const override;

    /// \brief Publishes the next frame's snapshot. Called by the frame loop only.
    ///
    /// \param realDeltaSeconds wall-clock delta, already clamped by the clock.
    /// \param fixedStepsThisFrame fixed steps the caller is about to execute.
    /// \param interpolationAlpha render interpolation factor in `[0, 1)`.
    /// \param fixedStepIndex authoritative tick count after those steps.
    /// \param fixedDeltaSeconds length of one fixed step.
    void beginFrame(double realDeltaSeconds,
                    std::uint32_t fixedStepsThisFrame,
                    float interpolationAlpha,
                    std::uint64_t fixedStepIndex,
                    double fixedDeltaSeconds) noexcept;

    /// \brief Discards the mutable state of the current frame. Called by the loop.
    void endFrame() noexcept;

private:
    struct DomainState {
        float scale = 1.0f;
        bool paused = false;
    };

    const DomainState& state(TimeDomain domain) const noexcept;

    DomainState m_domains[kTimeDomainCount]{};
    FrameContext m_context{};
};

}  // namespace fa::time
