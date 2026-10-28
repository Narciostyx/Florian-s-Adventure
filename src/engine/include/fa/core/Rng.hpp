#pragma once

#include <cstddef>
#include <cstdint>

namespace fa::core {

/// PCG32 (XSH-RR). Deterministic across platforms and compilers, which is what
/// makes replays, procedural generation and tests reproducible. The whole state is
/// two 64-bit words, so it drops straight into a save file.
class Rng {
public:
    static constexpr std::uint64_t kDefaultSeed = 0x853c49e6748fea9bULL;
    static constexpr std::uint64_t kDefaultStream = 0xda3e39cb94b95bdbULL;

    /// Produces a fixed, reproducible sequence - convenient for tests, never for
    /// gameplay, where each system should own a distinctly seeded Rng.
    Rng() noexcept;
    explicit Rng(std::uint64_t seed) noexcept;
    Rng(std::uint64_t seed, std::uint64_t stream) noexcept;

    void seed(std::uint64_t seed) noexcept;
    void seed(std::uint64_t seed, std::uint64_t stream) noexcept;

    std::uint64_t state() const noexcept { return m_state; }
    std::uint64_t increment() const noexcept { return m_increment; }
    void restore(std::uint64_t state, std::uint64_t increment) noexcept;

    std::uint32_t nextU32() noexcept;

    /// Uniform in [0, bound). Unbiased: out-of-range draws are rejected rather
    /// than folded, so the distribution does not drift. bound == 0 returns 0.
    std::uint32_t bounded(std::uint32_t bound) noexcept;

    /// Uniform in [minInclusive, maxExclusive). Returns minInclusive for an empty
    /// range instead of asserting, because gameplay code often passes dynamic sizes.
    int range(int minInclusive, int maxExclusive) noexcept;

    /// Uniform in [0, 1). 24 bits of mantissa, matching a float's precision.
    float nextFloat01() noexcept;
    float nextFloatRange(float minInclusive, float maxExclusive) noexcept;

    /// True with probability `probability`; clamps outside [0, 1].
    bool chance(float probability) noexcept;

    /// Uniform index in [0, count). Returns 0 when count == 0.
    std::size_t nextIndex(std::size_t count) noexcept;

private:
    std::uint64_t m_state = 0;
    std::uint64_t m_increment = 0;
};

}  // namespace fa::core
