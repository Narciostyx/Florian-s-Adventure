#pragma once

namespace fa::player::player_error {

/// \file
/// Error codes for M16. M17 owns 7000-7012; the controller continues at 7013.

/// \brief Another action is still running (attack/tool lock or a dodge in progress).
inline constexpr int kBusy = 7013;

/// \brief The dodge cooldown has not expired.
inline constexpr int kCooldown = 7014;

/// \brief Not enough stamina for the requested action.
inline constexpr int kInsufficientStamina = 7015;

/// \brief A dodge was requested without a movement direction.
inline constexpr int kNoDirection = 7016;

/// \brief The player is dead and ignores input.
inline constexpr int kDead = 7017;

/// \brief The entity is missing a required player component.
inline constexpr int kInvalidPlayer = 7018;

}  // namespace fa::player::player_error
