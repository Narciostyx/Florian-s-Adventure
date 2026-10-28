#include "fa/events/JobSystem.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include <fa/core/Assert.hpp>
#include <fa/core/Log.hpp>

namespace fa::events::detail {

/// \brief Per-job shared state. The handle, the worker and the completion callback
/// all reach it through a shared_ptr, so a handle can outlive the queue entry.
struct JobState {
    std::atomic<bool> complete{false};
    core::Status status = core::Status::ok(core::Unit{});
    std::string name;
    std::mutex mutex;
    std::condition_variable ready;
    std::function<void(core::Status)> completion;
};

struct CompletionRecord {
    CompletionRecord(std::function<void(core::Status)> callbackIn, core::Status statusIn)
        : callback(std::move(callbackIn)), status(std::move(statusIn)) {}

    std::function<void(core::Status)> callback;
    core::Status status;
};

struct JobSystemImpl {
    std::uint32_t workerCount = 1;
    std::vector<std::thread> workers;

    std::deque<std::function<void()>> queue;
    mutable std::mutex queueMutex;
    std::condition_variable queueCv;
    bool stopping = false;

    std::mutex completedMutex;
    std::vector<CompletionRecord> completed;
};

}  // namespace fa::events::detail

namespace fa::events {
namespace {

std::uint32_t resolveWorkerCount(std::uint32_t requested) noexcept {
    if (requested != kAutoWorkerCount) {
        return requested > kMaxWorkerCount ? kMaxWorkerCount : requested;
    }
    // Leave one core for the frame thread; hardware_concurrency() may report 0.
    const unsigned hardware = std::thread::hardware_concurrency();
    if (hardware <= 1) {
        return 1;
    }
    const std::uint32_t derived = hardware - 1;
    return derived > kMaxWorkerCount ? kMaxWorkerCount : derived;
}

/// \brief Pops and runs one queued task. \return false when the queue was empty.
///
/// Used by the worker loop (after a blocking wait) and by parallelFor's drain loop,
/// where running a queued task is what keeps a nested parallelFor from deadlocking.
bool tryRunOneTask(detail::JobSystemImpl& impl) {
    std::function<void()> task;
    {
        std::scoped_lock lock(impl.queueMutex);
        if (impl.queue.empty()) {
            return false;
        }
        task = std::move(impl.queue.front());
        impl.queue.pop_front();
    }
    task();
    return true;
}

void workerLoop(detail::JobSystemImpl& impl) {
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(impl.queueMutex);
            impl.queueCv.wait(lock, [&impl] { return impl.stopping || !impl.queue.empty(); });
            if (impl.queue.empty()) {
                if (impl.stopping) {
                    return;
                }
                continue;
            }
            task = std::move(impl.queue.front());
            impl.queue.pop_front();
        }
        task();
    }
}

/// \brief Shared state of one parallelFor call.
///
/// Chunk indices are claimed with an atomic counter, so no per-chunk bookkeeping and
/// no work stealing is needed. The first failure wins and stops further claims; chunks
/// already running are allowed to finish.
struct ParallelBatch {
    const std::function<core::Status(std::size_t)>* body = nullptr;
    std::size_t count = 0;
    std::size_t chunkSize = 1;
    std::size_t chunkCount = 0;

    std::atomic<std::size_t> nextChunk{0};
    std::atomic<bool> failed{false};
    std::atomic<std::size_t> helpersRunning{0};

    std::mutex errorMutex;
    core::Error firstError;

    void recordError(const core::Error& error) {
        std::scoped_lock lock(errorMutex);
        if (!failed.load(std::memory_order_relaxed)) {
            firstError = error;
            failed.store(true, std::memory_order_relaxed);
        }
    }

    void runChunks() {
        for (;;) {
            if (failed.load(std::memory_order_relaxed)) {
                return;
            }
            const std::size_t chunk = nextChunk.fetch_add(1, std::memory_order_relaxed);
            if (chunk >= chunkCount) {
                return;
            }
            const std::size_t begin = chunk * chunkSize;
            const std::size_t end = std::min(begin + chunkSize, count);
            for (std::size_t index = begin; index < end; ++index) {
                const core::Status status = (*body)(index);
                if (!status) {
                    recordError(status.error());
                    return;
                }
            }
        }
    }
};

}  // namespace

bool JobHandle::isComplete() const noexcept {
    return m_state != nullptr && m_state->complete.load(std::memory_order_acquire);
}

JobSystem::JobSystem(std::uint32_t workerCount) : m_impl(std::make_unique<detail::JobSystemImpl>()) {
    m_impl->workerCount = resolveWorkerCount(workerCount);
    m_impl->workers.reserve(m_impl->workerCount);
    for (std::uint32_t i = 0; i < m_impl->workerCount; ++i) {
        m_impl->workers.emplace_back([impl = m_impl.get()] { workerLoop(*impl); });
    }
    core::logf(
        core::LogCategory::Events, core::LogLevel::Info, "JobSystem: %u worker thread(s) started", m_impl->workerCount);
}

JobSystem::~JobSystem() {
    {
        std::scoped_lock lock(m_impl->queueMutex);
        m_impl->stopping = true;
    }
    m_impl->queueCv.notify_all();
    // Workers drain whatever is already queued before leaving, so shutdown never
    // silently drops an accepted job.
    for (std::thread& worker : m_impl->workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

JobHandle JobSystem::submit(JobDesc desc) {
    FA_ASSERT(desc.run != nullptr, "JobSystem::submit requires a callable job body");

    auto state = std::make_shared<detail::JobState>();
    state->name = std::move(desc.name);

    auto task = [impl = m_impl.get(), state, body = std::move(desc.run)]() {
        const core::Status status = body ? body() : core::Status::ok(core::Unit{});

        bool logFailure = false;
        {
            std::scoped_lock lock(state->mutex);
            state->status = status;
            std::function<void(core::Status)> completion = std::move(state->completion);
            if (completion) {
                /// Queued while `complete` is still false, so a wait() that returns
                /// guarantees the callback is already waiting for drainCompletedJobs().
                std::scoped_lock completedLock(impl->completedMutex);
                impl->completed.emplace_back(std::move(completion), status);
            } else {
                logFailure = !status && !state->name.empty();
            }
            state->complete.store(true, std::memory_order_release);
        }
        state->ready.notify_all();

        if (logFailure) {
            core::logf(core::LogCategory::Events,
                       core::LogLevel::Error,
                       "JobSystem: job '%s' failed: %s",
                       state->name.c_str(),
                       status.error().message.c_str());
        }
    };

    {
        std::scoped_lock lock(m_impl->queueMutex);
        m_impl->queue.push_back(std::move(task));
    }
    m_impl->queueCv.notify_one();
    return JobHandle(std::move(state));
}

core::Status JobSystem::wait(JobHandle handle) {
    if (!handle.isValid()) {
        return core::Status::err(core::error_code::kInvalidArgument, "JobSystem::wait: handle is not valid");
    }

    detail::JobState& state = *handle.m_state;
    std::unique_lock<std::mutex> lock(state.mutex);
    state.ready.wait(lock, [&state] { return state.complete.load(std::memory_order_acquire); });
    return state.status;
}

core::Status
JobSystem::parallelForErased(std::size_t count, std::size_t grain, const std::function<core::Status(std::size_t)>& fn) {
    if (count == 0) {
        return core::okStatus();
    }
    if (!fn) {
        return core::Status::err(core::error_code::kInvalidArgument, "JobSystem::parallelFor: body is empty");
    }

    ParallelBatch batch;
    batch.body = &fn;
    batch.count = count;
    batch.chunkSize = grain == 0 ? 1 : grain;
    batch.chunkCount = (count + batch.chunkSize - 1) / batch.chunkSize;

    // One helper per worker, but never more helpers than chunks. The calling thread
    // pulls chunks too, so a single-worker pool still finishes and never deadlocks.
    const std::size_t helperCount = std::min<std::size_t>(m_impl->workerCount, batch.chunkCount);
    batch.helpersRunning.store(helperCount, std::memory_order_relaxed);
    auto helper = [&batch] {
        batch.runChunks();
        batch.helpersRunning.fetch_sub(1, std::memory_order_acq_rel);
    };
    {
        std::scoped_lock lock(m_impl->queueMutex);
        for (std::size_t i = 0; i < helperCount; ++i) {
            m_impl->queue.push_back(helper);
        }
    }
    m_impl->queueCv.notify_all();

    batch.runChunks();

    // Drain the queue while waiting instead of blocking: if every worker is itself
    // inside a nested parallelFor, this thread runs their helpers and breaks the cycle.
    while (batch.helpersRunning.load(std::memory_order_acquire) != 0) {
        if (!tryRunOneTask(*m_impl)) {
            std::this_thread::yield();
        }
    }

    if (batch.failed.load(std::memory_order_relaxed)) {
        return core::Status::err(batch.firstError.code, batch.firstError.message);
    }
    return core::okStatus();
}

void JobSystem::onComplete(JobHandle handle, std::function<void(core::Status)> callback) {
    if (!handle.isValid() || !callback) {
        return;
    }

    detail::JobState& state = *handle.m_state;
    std::scoped_lock lock(state.mutex);
    if (state.complete.load(std::memory_order_acquire)) {
        detail::CompletionRecord record(std::move(callback), state.status);
        std::scoped_lock completedLock(m_impl->completedMutex);
        m_impl->completed.push_back(std::move(record));
        return;
    }
    state.completion = std::move(callback);
}

std::size_t JobSystem::drainCompletedJobs() {
    std::vector<detail::CompletionRecord> pending;
    {
        std::scoped_lock lock(m_impl->completedMutex);
        pending.swap(m_impl->completed);
    }
    for (detail::CompletionRecord& record : pending) {
        record.callback(record.status);
    }
    return pending.size();
}

std::uint32_t JobSystem::workerCount() const noexcept {
    return m_impl->workerCount;
}

std::size_t JobSystem::queuedJobCount() const noexcept {
    std::scoped_lock lock(m_impl->queueMutex);
    return m_impl->queue.size();
}

}  // namespace fa::events
