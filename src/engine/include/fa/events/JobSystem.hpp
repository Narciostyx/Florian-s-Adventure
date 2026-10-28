#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include <fa/core/Result.hpp>

namespace fa::events {

/// \brief Worker count that asks the pool to size itself from the machine.
inline constexpr std::uint32_t kAutoWorkerCount = 0;

/// \brief Hard ceiling so a bad config cannot spawn hundreds of threads.
inline constexpr std::uint32_t kMaxWorkerCount = 32;

namespace detail {
struct JobState;
struct JobSystemImpl;
}  // namespace detail

/// \brief Copyable, non-owning reference to one submitted job.
///
/// Valid for as long as any handle to it exists; the JobSystem that produced it may
/// be destroyed first, after which only isComplete()/status lookups remain legal.
class JobHandle final {
public:
    JobHandle() = default;

    bool isValid() const noexcept { return m_state != nullptr; }

    /// \brief Cheap non-blocking poll. Safe from any thread.
    bool isComplete() const noexcept;

private:
    friend class JobSystem;
    explicit JobHandle(std::shared_ptr<detail::JobState> state) noexcept : m_state(std::move(state)) {}

    std::shared_ptr<detail::JobState> m_state;
};

/// \brief One unit of background work.
///
/// `run` must not throw: exceptions are disabled solution-wide (`/EHsc-`), so a
/// failure has to come back as a Status. `name` is diagnostics only and shows up in
/// failure log lines.
struct JobDesc {
    std::string name{};
    std::function<core::Status()> run{};
};

/// \brief Fixed-size thread pool for CPU work that must leave the frame thread.
///
/// Ownership: one long-lived instance, created by the composition root after the log
/// is usable and destroyed before it. Threads are joined in the destructor, so a
/// submit() racing shutdown cannot leak a worker.
///
/// Waiting rules (docs/modules/03-events-and-jobs.md):
///   - wait() is for the frame thread. Calling it from a worker is only safe when the
///     awaited job is guaranteed to be picked up by a thread that is not waiting.
///   - parallelFor() is safe from the frame thread and from a worker: the calling
///     thread pulls chunks itself and drains the queue while waiting, so nesting does
///     not deadlock.
class JobSystem final {
public:
    explicit JobSystem(std::uint32_t workerCount = kAutoWorkerCount);
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    /// \brief Queues `desc`. The job starts as soon as a worker is free.
    JobHandle submit(JobDesc desc);

    /// \brief Blocks until the job finished, then returns its Status.
    /// An invalid handle yields error_code::kInvalidArgument instead of blocking.
    ///
    /// Once this returns, a callback registered with onComplete() is already queued,
    /// so a drainCompletedJobs() call after it is guaranteed to run that callback.
    core::Status wait(JobHandle handle);

    /// \brief Runs fn(index) for every index in [0, count), split into `grain` chunks.
    ///
    /// Returns the first failure, if any; remaining chunks are abandoned. `grain` of 0
    /// is treated as 1. This is the type-erased form: prefer the parallelFor template.
    core::Status
    parallelForErased(std::size_t count, std::size_t grain, const std::function<core::Status(std::size_t)>& fn);

    /// \brief parallelFor over a lambda. A void-returning body is treated as "no
    /// recoverable failure"; a body returning core::Status propagates its error.
    template <typename Fn>
    core::Status parallelFor(std::size_t count, std::size_t grain, Fn&& fn) {
        static_assert(std::is_invocable_v<Fn&, std::size_t>, "parallelFor body must accept a std::size_t index");
        if (count == 0) {
            return core::okStatus();
        }
        std::function<core::Status(std::size_t)> body;
        if constexpr (std::is_void_v<std::invoke_result_t<Fn&, std::size_t>>) {
            body = [callback = std::forward<Fn>(fn)](std::size_t index) mutable -> core::Status {
                callback(index);
                return core::okStatus();
            };
        } else {
            body = std::forward<Fn>(fn);
        }
        return parallelForErased(count, grain, body);
    }

    /// \brief Registers a callback to run on the thread that calls drainCompletedJobs().
    ///
    /// One callback per job in v1; a second registration replaces the first. GPU work
    /// that must happen on the frame thread (texture upload, buffer creation) belongs
    /// here rather than in the job body.
    void onComplete(JobHandle handle, std::function<void(core::Status)> callback);

    /// \brief Invokes every completion callback that is ready. Call once per frame.
    /// \return How many callbacks ran.
    std::size_t drainCompletedJobs();

    /// \brief Threads actually running jobs (never 0).
    std::uint32_t workerCount() const noexcept;

    /// \brief Jobs submitted but not yet picked up by a worker.
    std::size_t queuedJobCount() const noexcept;

private:
    std::unique_ptr<detail::JobSystemImpl> m_impl;
};

}  // namespace fa::events
