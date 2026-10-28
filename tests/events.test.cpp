/// \file
/// \brief Tests for M03: the typed event bus and the job pool.

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include <fa/core/Error.hpp>
#include <fa/core/Result.hpp>
#include <fa/events/EventBus.hpp>
#include <fa/events/JobSystem.hpp>

namespace {

using fa::core::Status;
using fa::events::EventBus;
using fa::events::JobDesc;
using fa::events::JobHandle;
using fa::events::JobSystem;
using fa::events::kAutoWorkerCount;
using fa::events::kInvalidSubscriptionId;
using fa::events::kMaxWorkerCount;
using fa::events::Subscription;

/// \brief Trivial payload types; the bus never inspects them.
struct Ping {
    int value = 0;
};

struct Pong {
    int value = 0;
};

struct Other {
    int value = 0;
};

Status failure(int code, const char* message) {
    return Status::err(code, message);
}

}  // namespace

TEST_CASE("EventBus publish with no subscribers invokes nothing", "[events][bus]") {
    EventBus bus;
    REQUIRE(bus.publish(Ping{1}) == 0);
    REQUIRE(bus.subscriberCount<Ping>() == 0);
    REQUIRE(bus.totalSubscriberCount() == 0);
}

TEST_CASE("EventBus delivers to every subscriber in registration order", "[events][bus]") {
    EventBus bus;
    std::vector<int> order;

    bus.subscribe<Ping>([&order](const Ping& event) { order.push_back(event.value * 10 + 1); });
    bus.subscribe<Ping>([&order](const Ping& event) { order.push_back(event.value * 10 + 2); });

    const std::size_t invoked = bus.publish(Ping{4});

    REQUIRE(invoked == 2);
    REQUIRE(order == std::vector<int>{41, 42});
}

TEST_CASE("EventBus keeps event types isolated", "[events][bus]") {
    EventBus bus;
    int pings = 0;
    int pongs = 0;

    bus.subscribe<Ping>([&pings](const Ping&) { ++pings; });
    bus.subscribe<Pong>([&pongs](const Pong&) { ++pongs; });

    REQUIRE(bus.publish(Ping{1}) == 1);
    REQUIRE(pings == 1);
    REQUIRE(pongs == 0);

    REQUIRE(bus.publish(Pong{1}) == 1);
    REQUIRE(pings == 1);
    REQUIRE(pongs == 1);

    REQUIRE(bus.subscriberCount<Ping>() == 1);
    REQUIRE(bus.subscriberCount<Pong>() == 1);
}

TEST_CASE("EventBus unsubscribe stops delivery and is idempotent", "[events][bus]") {
    EventBus bus;
    int calls = 0;

    const auto first = bus.subscribe<Ping>([&calls](const Ping&) { ++calls; });
    const auto second = bus.subscribe<Ping>([&calls](const Ping&) { ++calls; });
    REQUIRE(first != kInvalidSubscriptionId);
    REQUIRE(first != second);

    bus.unsubscribe(first);
    REQUIRE(bus.publish(Ping{1}) == 1);
    REQUIRE(calls == 1);

    bus.unsubscribe(first);
    bus.unsubscribe(kInvalidSubscriptionId);
    bus.unsubscribe(9999);
    REQUIRE(bus.publish(Ping{2}) == 1);
    REQUIRE(calls == 2);

    bus.unsubscribe(second);
    REQUIRE(bus.publish(Ping{3}) == 0);
    REQUIRE(calls == 2);
    REQUIRE(bus.subscriberCount<Ping>() == 0);
}

TEST_CASE("EventBus Subscription token unsubscribes on destruction", "[events][bus]") {
    EventBus bus;
    int calls = 0;

    {
        Subscription token(bus, bus.subscribe<Ping>([&calls](const Ping&) { ++calls; }));
        REQUIRE(token.isValid());
        REQUIRE(bus.publish(Ping{1}) == 1);
        REQUIRE(calls == 1);
    }

    REQUIRE(bus.publish(Ping{2}) == 0);
    REQUIRE(calls == 1);
    REQUIRE(bus.subscriberCount<Ping>() == 0);
}

TEST_CASE("EventBus Subscription token release keeps the subscription alive", "[events][bus]") {
    EventBus bus;
    int calls = 0;

    Subscription token(bus, bus.subscribe<Ping>([&calls](const Ping&) { ++calls; }));
    const auto id = token.release();
    REQUIRE_FALSE(token.isValid());

    REQUIRE(bus.publish(Ping{1}) == 1);
    REQUIRE(calls == 1);

    bus.unsubscribe(id);
    REQUIRE(bus.publish(Ping{2}) == 0);
}

TEST_CASE("EventBus subscription created during dispatch starts after that dispatch", "[events][bus]") {
    EventBus bus;
    int outerCalls = 0;
    int lateCalls = 0;
    bool subscribed = false;

    bus.subscribe<Ping>([&](const Ping&) {
        ++outerCalls;
        if (!subscribed) {
            subscribed = true;
            bus.subscribe<Ping>([&lateCalls](const Ping&) { ++lateCalls; });
        }
    });

    REQUIRE(bus.publish(Ping{1}) == 1);
    REQUIRE(outerCalls == 1);
    REQUIRE(lateCalls == 0);

    REQUIRE(bus.publish(Ping{2}) == 2);
    REQUIRE(outerCalls == 2);
    REQUIRE(lateCalls == 1);
}

TEST_CASE("EventBus unsubscribe during dispatch skips the remaining handler", "[events][bus]") {
    EventBus bus;
    int firstCalls = 0;
    int secondCalls = 0;
    auto secondId = kInvalidSubscriptionId;

    bus.subscribe<Ping>([&](const Ping&) {
        ++firstCalls;
        bus.unsubscribe(secondId);
    });
    secondId = bus.subscribe<Ping>([&secondCalls](const Ping&) { ++secondCalls; });

    REQUIRE(bus.publish(Ping{1}) == 1);
    REQUIRE(firstCalls == 1);
    REQUIRE(secondCalls == 0);
    REQUIRE(bus.subscriberCount<Ping>() == 1);
}

TEST_CASE("EventBus nested publish of the same type reenters safely", "[events][bus]") {
    EventBus bus;
    int calls = 0;
    bool nested = false;

    bus.subscribe<Ping>([&](const Ping&) {
        ++calls;
        if (!nested) {
            nested = true;
            REQUIRE(bus.publish(Ping{1}) == 1);
        }
    });

    REQUIRE(bus.publish(Ping{1}) == 1);
    REQUIRE(calls == 2);
    REQUIRE(bus.subscriberCount<Ping>() == 1);
}

TEST_CASE("EventBus enqueue defers delivery until dispatchQueued", "[events][bus]") {
    EventBus bus;
    int calls = 0;
    bus.subscribe<Ping>([&calls](const Ping&) { ++calls; });

    bus.enqueue(Ping{7});
    bus.enqueue(Ping{8});
    REQUIRE(calls == 0);
    REQUIRE(bus.queuedCount() == 2);

    REQUIRE(bus.dispatchQueued() == 2);
    REQUIRE(calls == 2);
    REQUIRE(bus.queuedCount() == 0);

    REQUIRE(bus.dispatchQueued() == 0);
}

TEST_CASE("EventBus dispatchQueued keeps enqueue order across types", "[events][bus]") {
    EventBus bus;
    std::vector<std::string> order;

    bus.subscribe<Ping>([&order](const Ping&) { order.emplace_back("ping"); });
    bus.subscribe<Pong>([&order](const Pong&) { order.emplace_back("pong"); });

    bus.enqueue(Pong{1});
    bus.enqueue(Ping{1});
    bus.enqueue(Pong{2});

    REQUIRE(bus.dispatchQueued() == 3);
    REQUIRE(order == std::vector<std::string>{"pong", "ping", "pong"});
}

TEST_CASE("EventBus clear drops queued events and subscriptions", "[events][bus]") {
    EventBus bus;
    int calls = 0;
    bus.subscribe<Ping>([&calls](const Ping&) { ++calls; });
    bus.enqueue(Ping{1});

    bus.clear();

    REQUIRE(bus.queuedCount() == 0);
    REQUIRE(bus.totalSubscriberCount() == 0);
    REQUIRE(bus.dispatchQueued() == 0);
    REQUIRE(bus.publish(Ping{2}) == 0);
    REQUIRE(calls == 0);
}

TEST_CASE("EventBus accepts enqueue from worker threads", "[events][bus]") {
    EventBus bus;
    int calls = 0;
    bus.subscribe<Ping>([&calls](const Ping&) { ++calls; });

    constexpr int kThreads = 4;
    constexpr int kPerThread = 100;
    std::vector<std::thread> producers;
    producers.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) {
        producers.emplace_back([&bus] {
            for (int n = 0; n < kPerThread; ++n) {
                bus.enqueue(Ping{n});
            }
        });
    }
    for (std::thread& producer : producers) {
        producer.join();
    }

    REQUIRE(bus.queuedCount() == static_cast<std::size_t>(kThreads * kPerThread));
    REQUIRE(bus.dispatchQueued() == static_cast<std::size_t>(kThreads * kPerThread));
    REQUIRE(calls == kThreads * kPerThread);
}

TEST_CASE("EventBus reports type ids and names for diagnostics", "[events][bus]") {
    const auto pingId = fa::events::detail::eventTypeIdOf<Ping>();
    const auto otherId = fa::events::detail::eventTypeIdOf<Other>();

    REQUIRE(pingId != 0);
    REQUIRE(pingId != otherId);
    REQUIRE(fa::events::detail::eventTypeIdOf<Ping>() == pingId);

    const std::string name{fa::events::eventTypeName<Ping>()};
    REQUIRE(name.find("Ping") != std::string::npos);
    REQUIRE(name.find("struct") == std::string::npos);
    REQUIRE(fa::events::eventTypeName<Other>() != fa::events::eventTypeName<Ping>());
}

TEST_CASE("JobSystem runs a submitted job and reports success", "[events][jobs]") {
    JobSystem jobs(1);
    REQUIRE(jobs.workerCount() == 1);

    std::atomic<int> ran{0};
    JobDesc desc;
    desc.name = "increment";
    desc.run = [&ran] {
        ran.fetch_add(1, std::memory_order_relaxed);
        return fa::core::okStatus();
    };

    const JobHandle handle = jobs.submit(std::move(desc));
    REQUIRE(handle.isValid());

    const Status status = jobs.wait(handle);
    REQUIRE(status.isOk());
    REQUIRE(ran.load() == 1);
    REQUIRE(handle.isComplete());
}

TEST_CASE("JobSystem returns the failure of a job body", "[events][jobs]") {
    JobSystem jobs(1);

    JobDesc desc;
    desc.name = "always-fails";
    desc.run = [] { return failure(7002, "job exploded"); };

    const Status status = jobs.wait(jobs.submit(std::move(desc)));
    REQUIRE(status.isError());
    REQUIRE(status.error().code == 7002);
    REQUIRE(status.error().message == "job exploded");
}

TEST_CASE("JobSystem wait rejects an invalid handle", "[events][jobs]") {
    JobSystem jobs(1);
    const Status status = jobs.wait(JobHandle{});
    REQUIRE(status.isError());
    REQUIRE(status.error().code == fa::core::error_code::kInvalidArgument);
}

TEST_CASE("JobHandle isComplete flips once the job finishes", "[events][jobs]") {
    JobSystem jobs(1);

    std::atomic<bool> gate{false};
    JobDesc desc;
    desc.name = "gated";
    desc.run = [&gate] {
        while (!gate.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        return fa::core::okStatus();
    };

    const JobHandle handle = jobs.submit(std::move(desc));
    REQUIRE_FALSE(handle.isComplete());

    gate.store(true, std::memory_order_release);
    REQUIRE(jobs.wait(handle).isOk());
    REQUIRE(handle.isComplete());
}

TEST_CASE("JobSystem parallelFor visits every index exactly once on one worker", "[events][jobs]") {
    JobSystem jobs(1);

    constexpr std::size_t kCount = 64;
    std::vector<std::atomic<int>> visits(kCount);

    const Status status = jobs.parallelFor(
        kCount, 8, [&visits](std::size_t index) { visits[index].fetch_add(1, std::memory_order_relaxed); });

    REQUIRE(status.isOk());
    for (const auto& visit : visits) {
        REQUIRE(visit.load() == 1);
    }
}

TEST_CASE("JobSystem parallelFor covers a large range across workers", "[events][jobs]") {
    JobSystem jobs(kAutoWorkerCount);

    constexpr std::size_t kCount = 1024;
    std::vector<std::atomic<int>> visits(kCount);

    const Status status = jobs.parallelFor(
        kCount, 7, [&visits](std::size_t index) { visits[index].fetch_add(1, std::memory_order_relaxed); });

    REQUIRE(status.isOk());
    for (const auto& visit : visits) {
        REQUIRE(visit.load() == 1);
    }
}

TEST_CASE("JobSystem parallelFor returns the first failure", "[events][jobs]") {
    JobSystem jobs(1);

    const Status status = jobs.parallelFor(64, 4, [](std::size_t index) -> Status {
        if (index == 13) {
            return failure(7003, "chunk failed");
        }
        return fa::core::okStatus();
    });

    REQUIRE(status.isError());
    REQUIRE(status.error().code == 7003);
}

TEST_CASE("JobSystem parallelFor with zero count does no work", "[events][jobs]") {
    JobSystem jobs(2);

    int calls = 0;
    const Status status = jobs.parallelFor(0, 4, [&calls](std::size_t) { ++calls; });

    REQUIRE(status.isOk());
    REQUIRE(calls == 0);
}

TEST_CASE("JobSystem parallelFor treats a zero grain as one", "[events][jobs]") {
    JobSystem jobs(2);

    std::atomic<int> calls{0};
    const Status status =
        jobs.parallelFor(5, 0, [&calls](std::size_t) { calls.fetch_add(1, std::memory_order_relaxed); });

    REQUIRE(status.isOk());
    REQUIRE(calls.load() == 5);
}

TEST_CASE("JobSystem onComplete fires on the thread that drains", "[events][jobs]") {
    JobSystem jobs(2);

    std::atomic<bool> fired{false};
    std::thread::id callbackThread;
    const std::thread::id frameThread = std::this_thread::get_id();

    JobDesc desc;
    desc.name = "with-completion";
    desc.run = [] { return fa::core::okStatus(); };

    const JobHandle handle = jobs.submit(std::move(desc));
    jobs.onComplete(handle, [&fired, &callbackThread](const Status& status) {
        REQUIRE(status.isOk());
        callbackThread = std::this_thread::get_id();
        fired.store(true, std::memory_order_release);
    });

    REQUIRE(jobs.wait(handle).isOk());

    std::size_t drained = 0;
    for (int attempt = 0; attempt < 1000 && drained == 0; ++attempt) {
        drained = jobs.drainCompletedJobs();
        std::this_thread::yield();
    }

    REQUIRE(drained == 1);
    REQUIRE(fired.load(std::memory_order_acquire));
    REQUIRE(callbackThread == frameThread);
    REQUIRE(jobs.drainCompletedJobs() == 0);
}

TEST_CASE("JobSystem resolves the worker count without ever returning zero", "[events][jobs]") {
    JobSystem autoSized(kAutoWorkerCount);
    REQUIRE(autoSized.workerCount() >= 1);

    JobSystem clamped(kMaxWorkerCount + 50);
    REQUIRE(clamped.workerCount() == kMaxWorkerCount);
}

TEST_CASE("JobSystem drains queued jobs when it is destroyed", "[events][jobs]") {
    std::atomic<int> ran{0};
    constexpr int kJobCount = 32;

    {
        JobSystem jobs(2);
        for (int i = 0; i < kJobCount; ++i) {
            JobDesc desc;
            desc.name = "drain-me";
            desc.run = [&ran] {
                ran.fetch_add(1, std::memory_order_relaxed);
                return fa::core::okStatus();
            };
            jobs.submit(std::move(desc));
        }
    }

    REQUIRE(ran.load() == kJobCount);
}
