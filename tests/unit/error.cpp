#include <jetio/error.hpp>

#include "support/contract_guard.hpp"
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

TEST_CASE("only an error can be reported", "[error]")
{
    if constexpr (contract_level >= 2)
    {
        test::scoped_throwing_contract_handler handler;
        error_code ec;
        CHECK_THROWS_AS(set_error_or_throw(&ec, error_code{}), test::contract_failure);
    }
}
