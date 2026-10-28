#include <fa/core/Hash.hpp>

namespace fa::core {

std::uint64_t hashBytes(const void* data, std::size_t size) noexcept {
    if (data == nullptr || size == 0) {
        return 0xcbf29ce484222325ULL;
    }
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= static_cast<std::uint64_t>(bytes[i]);
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

}  // namespace fa::core
