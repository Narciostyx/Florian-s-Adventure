// M00/M01 smoke entry point. Replaced by the real composition root once M02
// (platform window) and M04 (frame loop) exist.

#include <cstdio>

#include <fa/core/Version.hpp>

int main() {
    std::printf("Florian's Adventure - engine v%.*s (M01 core)\n", static_cast<int>(fa::core::engineVersion().size()),
                fa::core::engineVersion().data());
    return 0;
}
