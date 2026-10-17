/// fa_time - M00 placeholder translation unit.
/// Fixed-step frame loop (60 Hz, max 5 catch-up steps) lands with M04.

namespace fa::time {

constexpr double kFixedDeltaSeconds = 1.0 / 60.0;
constexpr int kMaxCatchUpSteps = 5;

double fixedDeltaSeconds() noexcept {
    return kFixedDeltaSeconds;
}

int maxCatchUpSteps() noexcept {
    return kMaxCatchUpSteps;
}

}  // namespace fa::time
