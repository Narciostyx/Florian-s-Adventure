#include <fa/core/Rng.hpp>

#include <fa/core/Assert.hpp>

namespace fa::core {
namespace {

constexpr std::uint64_t kMultiplier = 6364136223846793005ULL;

}  // namespace

Rng::Rng() noexcept {
    seed(kDefaultSeed, kDefaultStream);
}

Rng::Rng(std::uint64_t seedValue) noexcept {
    seed(seedValue, kDefaultStream);
}

Rng::Rng(std::uint64_t seedValue, std::uint64_t stream) noexcept {
    seed(seedValue, stream);
}

void Rng::seed(std::uint64_t seedValue) noexcept {
    seed(seedValue, kDefaultStream);
}

void Rng::seed(std::uint64_t seedValue, std::uint64_t stream) noexcept {
    m_state = 0;
    m_increment = (stream << 1U) | 1U;
    nextU32();
    m_state += seedValue;
    nextU32();
}

void Rng::restore(std::uint64_t stateValue, std::uint64_t increment) noexcept {
    /// \note The increment must stay odd; a corrupted save must not silently break
    /// the period guarantee, so force the low bit rather than trusting the input.
    m_state = stateValue;
    m_increment = increment | 1U;
}

std::uint32_t Rng::nextU32() noexcept {
    const std::uint64_t oldState = m_state;
    m_state = oldState * kMultiplier + m_increment;
    const std::uint32_t xorshifted = static_cast<std::uint32_t>(((oldState >> 18U) ^ oldState) >> 27U);
    const std::uint32_t rot = static_cast<std::uint32_t>(oldState >> 59U);
    return (xorshifted >> rot) | (xorshifted << ((0U - rot) & 31U));
}

std::uint32_t Rng::bounded(std::uint32_t bound) noexcept {
    /// \note A zero bound is a degenerate but legitimate gameplay case (empty
    /// candidate list), so it returns 0 instead of asserting: Debug and Release
    /// must agree.
    if (bound == 0) {
        return 0;
    }
    const std::uint32_t threshold = (0U - bound) % bound;
    for (;;) {
        const std::uint32_t drawn = nextU32();
        if (drawn >= threshold) {
            return drawn % bound;
        }
    }
}

int Rng::range(int minInclusive, int maxExclusive) noexcept {
    /// \note Empty or inverted spans collapse to minInclusive; see bounded() for
    /// why this is not an assertion failure.
    if (maxExclusive <= minInclusive) {
        return minInclusive;
    }
    const auto span = static_cast<std::uint32_t>(static_cast<std::uint64_t>(maxExclusive - minInclusive));
    return minInclusive + static_cast<int>(bounded(span));
}

float Rng::nextFloat01() noexcept {
    return static_cast<float>(nextU32() >> 8U) * (1.0f / 16777216.0f);
}

float Rng::nextFloatRange(float minInclusive, float maxExclusive) noexcept {
    return minInclusive + (maxExclusive - minInclusive) * nextFloat01();
}

bool Rng::chance(float probability) noexcept {
    if (probability <= 0.0f) {
        return false;
    }
    if (probability >= 1.0f) {
        return true;
    }
    return nextFloat01() < probability;
}

std::size_t Rng::nextIndex(std::size_t count) noexcept {
    FA_ASSERT(count <= 0xFFFFFFFFULL, "Rng::nextIndex count exceeds the 32-bit range");
    if (count == 0 || count > 0xFFFFFFFFULL) {
        return 0;
    }
    return static_cast<std::size_t>(bounded(static_cast<std::uint32_t>(count)));
}

}  // namespace fa::core
