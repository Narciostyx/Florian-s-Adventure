#include "fa/content/ContentTypes.hpp"

namespace fa::content {

std::uint8_t parseSeasonMask(std::string_view name) noexcept {
    if (name == "spring") {
        return seasonMask(Season::Spring);
    }
    if (name == "summer") {
        return seasonMask(Season::Summer);
    }
    if (name == "fall" || name == "autumn") {
        return seasonMask(Season::Fall);
    }
    if (name == "winter") {
        return seasonMask(Season::Winter);
    }
    if (name == "any") {
        return seasonMask(Season::Any);
    }
    return 0;
}

std::string_view toString(Season season) noexcept {
    switch (season) {
        case Season::Spring:
            return "spring";
        case Season::Summer:
            return "summer";
        case Season::Fall:
            return "fall";
        case Season::Winter:
            return "winter";
        case Season::Any:
            return "any";
        default:
            return "unknown";
    }
}

bool seasonAllows(std::uint8_t mask, Season season) noexcept {
    if (mask == 0) {
        return false;
    }
    return (mask & seasonMask(season)) != 0;
}

}  // namespace fa::content
