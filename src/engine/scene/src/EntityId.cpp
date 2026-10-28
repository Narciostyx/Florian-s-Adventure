#include "fa/scene/EntityId.hpp"

#include <cstdio>

namespace fa::scene {

std::string toString(EntityId id) {
    if (id.isPending()) {
        char buffer[32] = {};
        std::snprintf(buffer, sizeof(buffer), "e%upending", id.index);
        return buffer;
    }
    if (!id.isValid()) {
        return "e0";
    }
    char buffer[48] = {};
    std::snprintf(buffer, sizeof(buffer), "e%uv%u", id.index, id.generation);
    return buffer;
}

}  // namespace fa::scene
