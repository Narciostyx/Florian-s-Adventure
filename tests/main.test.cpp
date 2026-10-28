/// \file
/// \brief Catch2 entry point.
///
/// \note We supply main ourselves instead of linking Catch2::Catch2WithMain so the
/// test binary depends on exactly one vcpkg library (Catch2.lib).
#include <catch2/catch_session.hpp>

int main(int argc, char* argv[]) {
    return Catch::Session().run(argc, argv);
}
