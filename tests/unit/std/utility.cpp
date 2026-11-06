#include <jio/std/utility.hpp>

#include <string>

#include <catch2/catch_test_macros.hpp>

using namespace jio;

TEST_CASE("might_unused accepts any arguments", "[utility]")
{
    int x = 1;
    const std::string s = "s";
    might_unused();
    might_unused(x, s, 2.5, "the reason");
    might_unused(std::string("temporary"));
    CHECK(x == 1); // lvalues are not changed

    constexpr bool in_constant_expression = (might_unused(1, "reason"), true);
    static_assert(in_constant_expression);
    static_assert(noexcept(might_unused(1, 2)));
}
