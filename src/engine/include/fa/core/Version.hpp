#pragma once

#include <string_view>

namespace fa::core {

/// Engine version string, e.g. "0.1.0". Reported by logs, crash dumps and the
/// save-file header so old saves can be rejected with a useful message.
std::string_view engineVersion() noexcept;

}  // namespace fa::core
