#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace fa::core {

/// FNV-1a 64. Stable across runs, compilers and platforms - which is why it is used
/// for content ids and debug-name lookups. Not cryptographic and not
/// collision-resistant against hostile input; never use it for save integrity.
constexpr std::uint64_t fnv1a64(std::string_view text) noexcept {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const char c : text) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

/// Mixes one value into a running hash. Same constant as boost::hash_combine:
/// cheap, good enough avalanche for hash maps and set membership.
constexpr std::uint64_t hashCombine(std::uint64_t seed, std::uint64_t value) noexcept {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
    return seed;
}

/// Fallback for arbitrary binary blobs (content file bytes, component payloads).
std::uint64_t hashBytes(const void* data, std::size_t size) noexcept;

}  // namespace fa::core
