#include "fa/time/TimeDomain.hpp"

namespace fa::time {

std::string_view toString(TimeDomain domain) noexcept {
    switch (domain) {
        case TimeDomain::Gameplay:
            return "gameplay";
        case TimeDomain::Ui:
            return "ui";
        case TimeDomain::Cinematic:
            return "cinematic";
        case TimeDomain::Count:
            break;
    }
    return "unknown";
}

}  // namespace fa::time
