#include <jetio/error.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace jetio;

TEST_CASE("errors are reported through the error code", "[error]")
{
    error_code ec;
    set_error_or_throw(&ec, std::errc::invalid_argument);
    CHECK(ec == std::errc::invalid_argument);
    CHECK(has_error(&ec));

    clear_error(&ec);
    CHECK(!ec);
    CHECK(!has_error(&ec));
}

TEST_CASE("errors are thrown without an error code", "[error]")
{
    try
    {
        set_error_or_throw(nullptr, std::errc::value_too_large);
        FAIL("no exception");
    }
    catch (const io_error &e)
    {
        CHECK(e.code() == std::errc::value_too_large);
    }
}

TEST_CASE("a null error code is accepted everywhere", "[error]")
{
    clear_error(nullptr);
    CHECK(!has_error(nullptr));
    static_assert(noexcept(clear_error(nullptr)));
    static_assert(noexcept(has_error(nullptr)));
}
