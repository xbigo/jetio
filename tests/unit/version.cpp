#include <jetio/jetio.hpp>

#include <string>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("version matches the CMake project version", "[version]")
{
    const auto version = std::to_string(jetio::version_major) + '.' +
                         std::to_string(jetio::version_minor) + '.' +
                         std::to_string(jetio::version_patch);
    CHECK(version == JETIO_TEST_EXPECTED_VERSION);
}
