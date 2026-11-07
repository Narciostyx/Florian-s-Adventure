#pragma once

#include <cstddef>
#include <cstdint>

namespace fa::core {

/// \brief PCG32 (XSH-RR). Deterministic across platforms and compilers, which is what
/// makes replays, procedural generation and tests reproducible. The whole state is
/// two 64-bit words, so it drops straight into a save file.
class Rng {
public:
    static constexpr std::uint64_t kDefaultSeed = 0x853c49e6748fea9bULL;
    static constexpr std::uint64_t kDefaultStream = 0xda3e39cb94b95bdbULL;

    /// \brief Seeds with the documented defaults; deterministic, test-friendly - never
    /// for gameplay, where each system should own a distinctly seeded Rng.
    Rng() noexcept;
    /// \brief Seeds the stream with \p seed and the default stream constant.
    explicit Rng(std::uint64_t seed) noexcept;
    /// \brief Full two-word constructor: independent streams keep systems' sequences
    /// from overlapping after identical seeds.
    Rng(std::uint64_t seed, std::uint64_t stream) noexcept;

    /// \brief Restarts the sequence with \p seed and the default stream.
    void seed(std::uint64_t seed) noexcept;
    /// \brief Restarts the sequence with an explicit seed and stream.
    void seed(std::uint64_t seed, std::uint64_t stream) noexcept;

    /// \brief The two words that fully describe the generator; persist both together.
    /// @{
    std::uint64_t state() const noexcept { return m_state; }
    std::uint64_t increment() const noexcept { return m_increment; }
    /// \brief Replaces both words from a save; the next draw continues the old sequence.
    void restore(std::uint64_t state, std::uint64_t increment) noexcept;
    /// @}

    /// \brief Raw 32-bit output; the primitive every other draw builds on.
    std::uint32_t nextU32() noexcept;

    /// \brief Uniform in [0, bound). Unbiased: out-of-range draws are rejected rather
    /// than folded, so the distribution does not drift. bound == 0 returns 0.
    std::uint32_t bounded(std::uint32_t bound) noexcept;

    /// \brief Uniform in [minInclusive, maxExclusive). Returns minInclusive for an empty
    /// range instead of asserting, because gameplay code often passes dynamic sizes.
    int range(int minInclusive, int maxExclusive) noexcept;

    /// \brief Uniform in [0, 1). 24 bits of mantissa, matching a float's precision.
    float nextFloat01() noexcept;
    /// \brief Uniform in [minInclusive, maxExclusive); returns minInclusive when the
    /// range is empty or inverted.
    float nextFloatRange(float minInclusive, float maxExclusive) noexcept;

    /// \brief True with probability `probability`; clamps outside [0, 1].
    bool chance(float probability) noexcept;

    /// \brief Uniform index in [0, count). Returns 0 when count == 0.
    std::size_t nextIndex(std::size_t count) noexcept;

private:
    std::uint64_t m_state = 0;
    std::uint64_t m_increment = 0;
};

}  // namespace fa::core
