#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <fa/core/Error.hpp>
#include <fa/core/Hash.hpp>
#include <fa/core/Ids.hpp>
#include <fa/core/Log.hpp>
#include <fa/core/Math.hpp>
#include <fa/core/Result.hpp>
#include <fa/core/Rng.hpp>
#include <fa/core/TimeUnits.hpp>

using namespace fa::core;

namespace {

/// \brief Two tags so StrongId's type safety can be exercised without inventing real game ids.
struct ItemTag;
struct NpcTag;

using TestItemId = StrongId<ItemTag>;
using TestNpcId = StrongId<NpcTag>;

class CapturingSink final : public ILogSink {
public:
    void write(LogLevel, LogCategory, std::string_view message) override { messages.emplace_back(message); }

    std::vector<std::string> messages;
};

}  // namespace

TEST_CASE("Result carries either a value or an error", "[core][result]") {
    const auto ok = Result<int>::ok(7);
    REQUIRE(ok.isOk());
    REQUIRE_FALSE(ok.isError());
    REQUIRE(static_cast<bool>(ok));
    REQUIRE(ok.value() == 7);

    const auto failed = Result<int>::err(error_code::kNotFound, "no such tile");
    REQUIRE(failed.isError());
    REQUIRE_FALSE(static_cast<bool>(failed));
    REQUIRE(failed.error().code == error_code::kNotFound);
    REQUIRE(failed.error().message == "no such tile");

    REQUIRE(ok.valueOr(0) == 7);
    REQUIRE(failed.valueOr(0) == 0);
}

TEST_CASE("Result carries move-only payloads", "[core][result]") {
    auto unique = std::make_unique<int>(5);
    auto result = Result<std::unique_ptr<int>>::ok(std::move(unique));
    REQUIRE(result.isOk());
    REQUIRE(*result.value() == 5);
}

TEST_CASE("Status models success without a payload", "[core][result]") {
    const Status good = okStatus();
    REQUIRE(good.isOk());

    const Status bad = errorStatus(error_code::kIoFailure, "disk full");
    REQUIRE(bad.isError());
    REQUIRE(bad.error().code == error_code::kIoFailure);

    REQUIRE(good.ignoreValue().isOk());
    REQUIRE(bad.ignoreValue().isError());
}

TEST_CASE("Rng is deterministic for a given seed", "[core][rng]") {
    Rng a(1234);
    Rng b(1234);
    for (int i = 0; i < 32; ++i) {
        REQUIRE(a.nextU32() == b.nextU32());
    }

    Rng c(1235);
    REQUIRE(c.nextU32() != Rng(1234).nextU32());
}

TEST_CASE("Rng state round-trips through save data", "[core][rng]") {
    Rng original(99);
    for (int i = 0; i < 10; ++i) {
        original.nextU32();
    }

    Rng restored(0);
    restored.restore(original.state(), original.increment());
    for (int i = 0; i < 10; ++i) {
        REQUIRE(restored.nextU32() == original.nextU32());
    }
}

TEST_CASE("Rng.bounded stays inside the requested range", "[core][rng]") {
    Rng rng(7);
    for (int i = 0; i < 2000; ++i) {
        REQUIRE(rng.bounded(5) < 5U);
    }
    REQUIRE(rng.bounded(0) == 0U);
}

TEST_CASE("Rng.range is half-open", "[core][rng]") {
    Rng rng(11);
    for (int i = 0; i < 2000; ++i) {
        const int value = rng.range(-3, 4);
        REQUIRE(value >= -3);
        REQUIRE(value < 4);
    }
    REQUIRE(rng.range(5, 5) == 5);
}

TEST_CASE("Rng float helpers stay in range", "[core][rng]") {
    Rng rng(21);
    for (int i = 0; i < 2000; ++i) {
        const float unit = rng.nextFloat01();
        REQUIRE(unit >= 0.0f);
        REQUIRE(unit < 1.0f);

        const float ranged = rng.nextFloatRange(2.0f, 5.0f);
        REQUIRE(ranged >= 2.0f);
        REQUIRE(ranged < 5.0f);
    }

    REQUIRE_FALSE(rng.chance(0.0f));
    REQUIRE(rng.chance(1.0f));
}

TEST_CASE("Rng.nextIndex handles empty and non-empty collections", "[core][rng]") {
    Rng rng(3);
    REQUIRE(rng.nextIndex(0) == 0U);
    for (int i = 0; i < 500; ++i) {
        REQUIRE(rng.nextIndex(4) < 4U);
    }
}

TEST_CASE("Rects treat touching edges as non-overlapping", "[core][math]") {
    const Rect a = makeRect(0.0f, 0.0f, 10.0f, 10.0f);

    REQUIRE(rectOverlaps(a, makeRect(5.0f, 5.0f, 10.0f, 10.0f)));
    REQUIRE_FALSE(rectOverlaps(a, makeRect(10.0f, 0.0f, 5.0f, 5.0f)));
    REQUIRE_FALSE(rectOverlaps(a, makeRect(0.0f, 10.0f, 5.0f, 5.0f)));
    REQUIRE(rectOverlaps(a, a));
}

TEST_CASE("Rect helpers agree on edges and centres", "[core][math]") {
    const Rect r = makeRect(2.0f, 4.0f, 8.0f, 6.0f);

    REQUIRE(left(r) == 2.0f);
    REQUIRE(right(r) == 10.0f);
    REQUIRE(top(r) == 4.0f);
    REQUIRE(bottom(r) == 10.0f);
    REQUIRE(center(r).x == 6.0f);
    REQUIRE(center(r).y == 7.0f);
    REQUIRE_FALSE(isEmpty(r));

    const Rect centered = rectFromCenter(Vec2{10.0f, 10.0f}, 4.0f, 2.0f);
    REQUIRE(centered.x == 8.0f);
    REQUIRE(centered.y == 9.0f);
}

TEST_CASE("Rect containment is half-open", "[core][math]") {
    const Rect r = makeRect(0.0f, 0.0f, 4.0f, 4.0f);

    REQUIRE(rectContains(r, Vec2{0.0f, 0.0f}));
    REQUIRE(rectContains(r, Vec2{3.99f, 3.99f}));
    REQUIRE_FALSE(rectContains(r, Vec2{4.0f, 2.0f}));
    REQUIRE_FALSE(rectContains(r, Vec2{-0.01f, 2.0f}));

    REQUIRE(rectContainsRect(r, makeRect(1.0f, 1.0f, 2.0f, 2.0f)));
    REQUIRE_FALSE(rectContainsRect(r, makeRect(3.0f, 3.0f, 2.0f, 2.0f)));
}

TEST_CASE("Rect intersection collapses to empty when disjoint", "[core][math]") {
    const Rect overlap = intersect(makeRect(0.0f, 0.0f, 10.0f, 10.0f), makeRect(5.0f, 2.0f, 10.0f, 3.0f));
    REQUIRE(overlap.x == 5.0f);
    REQUIRE(overlap.y == 2.0f);
    REQUIRE(overlap.w == 5.0f);
    REQUIRE(overlap.h == 3.0f);

    REQUIRE(isEmpty(intersect(makeRect(0.0f, 0.0f, 1.0f, 1.0f), makeRect(5.0f, 5.0f, 1.0f, 1.0f))));
}

TEST_CASE("Interpolation and approach behave as documented", "[core][math]") {
    REQUIRE(lerp(0.0f, 10.0f, 0.5f) == 5.0f);
    REQUIRE(clamp01(-1.0f) == 0.0f);
    REQUIRE(clamp01(2.0f) == 1.0f);
    REQUIRE(clampf(5.0f, 0.0f, 4.0f) == 4.0f);

    REQUIRE(approach(0.0f, 10.0f, 3.0f) == 3.0f);
    REQUIRE(approach(0.0f, 10.0f, 100.0f) == 10.0f);
    REQUIRE(approach(10.0f, 0.0f, 4.0f) == 6.0f);

    const Vec2 mid = lerp(Vec2{0.0f, 0.0f}, Vec2{2.0f, 4.0f}, 0.5f);
    REQUIRE(mid.x == 1.0f);
    REQUIRE(mid.y == 2.0f);
}

TEST_CASE("FNV-1a matches the reference vectors", "[core][hash]") {
    REQUIRE(fnv1a64("") == 0xcbf29ce484222325ULL);
    REQUIRE(fnv1a64("a") == 0xaf63dc4c8601ec8cULL);
    REQUIRE(fnv1a64("foobar") == 0x85944171f73967e8ULL);
}

TEST_CASE("hashBytes agrees with fnv1a64", "[core][hash]") {
    constexpr std::string_view text = "tile:soil";
    REQUIRE(hashBytes(text.data(), text.size()) == fnv1a64(text));

    REQUIRE(hashBytes(nullptr, 0) == 0xcbf29ce484222325ULL);
}

TEST_CASE("hashCombine depends on both inputs and their order", "[core][hash]") {
    const std::uint64_t ab = hashCombine(hashCombine(0U, 1U), 2U);
    const std::uint64_t ba = hashCombine(hashCombine(0U, 2U), 1U);

    REQUIRE(ab == hashCombine(hashCombine(0U, 1U), 2U));
    REQUIRE(ab != ba);
}

TEST_CASE("StrongId is zero-invalid and hashable", "[core][ids]") {
    const TestItemId none;
    REQUIRE_FALSE(none.isValid());
    REQUIRE(none.value() == 0U);

    const TestItemId carrot(42U);
    const TestNpcId abigail(42U);
    REQUIRE(carrot.isValid());
    REQUIRE(carrot == TestItemId(42U));
    REQUIRE(carrot != none);
    /// \brief The point of StrongId: same integer, different tag, still distinct types.
    REQUIRE(carrot.value() == abigail.value());

    std::unordered_map<TestItemId, int> lookup;
    lookup[carrot] = 3;
    REQUIRE(lookup.at(TestItemId(42U)) == 3);
}

TEST_CASE("Countdown fires exactly once", "[core][time]") {
    Countdown timer(seconds(0.5));
    REQUIRE(timer.isRunning());

    REQUIRE_FALSE(timer.tick(seconds(0.25)));
    REQUIRE(timer.isRunning());

    REQUIRE(timer.tick(seconds(0.3)));
    REQUIRE_FALSE(timer.isRunning());
    REQUIRE_FALSE(timer.tick(seconds(1.0)));
    REQUIRE(timer.remaining().value == 0.0);
}

TEST_CASE("Time units convert both ways", "[core][time]") {
    REQUIRE(milliseconds(1500.0).toSeconds().value == 1.5);
    REQUIRE(seconds(1.5).toMilliseconds().value == 1500.0);
    REQUIRE(Countdown(seconds(2.0)).progress(2.0) == 0.0);
}

TEST_CASE("Log filters below the configured minimum level", "[core][log]") {
    CapturingSink sink;
    addLogSink(&sink);

    const LogLevel previous = logLevel();
    setLogLevel(LogLevel::Warn);

    log(LogCategory::Core, LogLevel::Info, "dropped");
    log(LogCategory::Core, LogLevel::Error, "kept");
    logf(LogCategory::Core, LogLevel::Error, "formatted %d", 7);

    setLogLevel(previous);
    removeLogSink(&sink);

    REQUIRE(sink.messages.size() == 2U);
    REQUIRE(sink.messages[0] == "kept");
    REQUIRE(sink.messages[1] == "formatted 7");
}

TEST_CASE("Removing a log sink stops delivery", "[core][log]") {
    CapturingSink sink;
    addLogSink(&sink);
    removeLogSink(&sink);

    const LogLevel previous = logLevel();
    setLogLevel(LogLevel::Trace);
    log(LogCategory::Core, LogLevel::Info, "after removal");
    setLogLevel(previous);

    REQUIRE(sink.messages.empty());
}
