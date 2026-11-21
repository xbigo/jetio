#include <jio/error.hpp>

#include <source_location>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

using namespace jio;

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

namespace
{
    struct bad_input : exception
    {
        using exception::exception;
    };
} // namespace

TEST_CASE("a failure can be thrown as a type of its own, with the place it was reported", "[error]")
{
    const auto here = std::source_location::current();
    try
    {
        set_error_or_throw<bad_input>(nullptr, std::errc::invalid_argument, "the input");
        FAIL("no exception");
    }
    catch (const bad_input &e)
    {
        CHECK(e.code() == std::errc::invalid_argument);
        CHECK(std::string(e.what()).starts_with("the input"));
        CHECK(std::string_view(e.where().file_name()) == here.file_name());
        CHECK(e.where().line() == here.line() + 3);
    }

    error_code ec;
    set_error_or_throw<bad_input>(&ec, std::errc::invalid_argument); // no exception with a code
    CHECK(ec == std::errc::invalid_argument);
}

TEST_CASE("io_error is one of the exceptions of the library", "[error]")
{
    static_assert(std::is_base_of_v<exception, io_error>);
    static_assert(std::is_base_of_v<std::system_error, exception>);
}
