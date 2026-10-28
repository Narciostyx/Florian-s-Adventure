#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace fa::time {

/// \brief Independent time domains, each with its own pause flag and scale.
///
/// They exist so that pausing the simulation can still animate menus, and a
/// cutscene can slow gameplay without touching UI timers.
enum class TimeDomain : std::uint8_t {
    Gameplay,
    Ui,
    Cinematic,
    Count,
};

/// \brief Number of usable domains; `TimeDomain::Count` is a sentinel, not a domain.
inline constexpr std::size_t kTimeDomainCount = static_cast<std::size_t>(TimeDomain::Count);

/// \brief Stable lowercase name, for logs and debug UI.
///
/// \return "gameplay", "ui", "cinematic", or "unknown" for an out-of-range value.
std::string_view toString(TimeDomain domain) noexcept;

}  // namespace fa::time
