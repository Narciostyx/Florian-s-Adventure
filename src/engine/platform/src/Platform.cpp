// fa_platform - M00 placeholder translation unit.
// SDL3 window/event wrapper lands with M02. Do not include SDL headers elsewhere.

#include <cstdint>

namespace fa::platform {

std::uint32_t platformApiVersion() noexcept {
    return 1;
}

}  // namespace fa::platform
