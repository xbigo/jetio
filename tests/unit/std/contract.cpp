// White-box tests of the contract facility. A violated contract cannot be continued, so the
// handler throws and the tests catch the exception.
#include <jetio/std/contract.hpp>

#include <optional>
#include <type_traits>

#include "support/contract_guard.hpp"
#include <catch2/catch_test_macros.hpp>

using jetio::contract_kind;
using jetio::test::contract_failure;

namespace
{
    constexpr bool checked(int min_level)
    {
        return jetio::contract_level >= min_level;
    }

    template <typename F> std::optional<contract_failure> violation_of(F &&f)
    {
        try
        {
            f();
        }
        catch (const contract_failure &failure)
        {
            return failure;
        }
        return std::nullopt;
    }

    // `f` evaluates its condition by incrementing the counter; the condition is false. The
    // check must be made, and the condition evaluated, exactly if the level reaches min_level.
    template <typename F> void expect_checked_from(int min_level, contract_kind kind, F &&f)
    {
        int evaluated = 0;
        const auto failure = violation_of([&] { f(evaluated); });
        CHECK(failure.has_value() == checked(min_level));
        CHECK(evaluated == (checked(min_level) ? 1 : 0));
        if (failure.has_value())
            CHECK(failure.value().kind == kind);
    }

    [[noreturn]] void some_handler(const jetio::contract_violation &v)
    {
        throw contract_failure(v);
    }
} // namespace

TEST_CASE("a satisfied contract is silent", "[contract]")
{
    jetio::test::scoped_throwing_contract_handler handler;
    CHECK_NOTHROW(JIO_Expects(1 + 1 == 2));
    CHECK_NOTHROW(JIO_Ensures(true));
    CHECK_NOTHROW(JIO_Assert(true));
    CHECK_NOTHROW(JIO_Expects_critical(true));
    CHECK_NOTHROW(JIO_Assert_critical(true));
    CHECK_NOTHROW(JIO_Expects_audit(true));
    CHECK_NOTHROW(JIO_Ensures_audit(true));
    CHECK_NOTHROW(JIO_Assert_audit(true));
}

// A check is made if the level reaches the level of its macro: critical 1, default 2, audit 3.
TEST_CASE("the level decides which checks are made", "[contract]")
{
    jetio::test::scoped_throwing_contract_handler handler;

    expect_checked_from(1, contract_kind::precondition,
                        [](int &n) { JIO_Expects_critical(++n < 0); });
    expect_checked_from(1, contract_kind::assertion, [](int &n) { JIO_Assert_critical(++n < 0); });

    expect_checked_from(2, contract_kind::precondition, [](int &n) { JIO_Expects(++n < 0); });
    expect_checked_from(2, contract_kind::postcondition, [](int &n) { JIO_Ensures(++n < 0); });
    expect_checked_from(2, contract_kind::assertion, [](int &n) { JIO_Assert(++n < 0); });

    expect_checked_from(3, contract_kind::precondition, [](int &n) { JIO_Expects_audit(++n < 0); });
    expect_checked_from(3, contract_kind::postcondition,
                        [](int &n) { JIO_Ensures_audit(++n < 0); });
    expect_checked_from(3, contract_kind::assertion, [](int &n) { JIO_Assert_audit(++n < 0); });
}

TEST_CASE("the violation says what and where", "[contract]")
{
    if constexpr (!checked(2))
        SKIP("default contract checks are off");

    jetio::test::scoped_throwing_contract_handler handler;
    try
    {
        JIO_Expects(1 > 2);
        FAIL("no violation");
    }
    catch (const contract_failure &failure)
    {
        CHECK(failure.expression == "1 > 2");
        CHECK(failure.line == __LINE__ - 6);
    }

    // a condition with commas outside parentheses is passed on whole
    try
    {
        JIO_Expects(std::is_same_v<int, long>);
        FAIL("no violation");
    }
    catch (const contract_failure &failure)
    {
        CHECK(failure.expression == "std::is_same_v<int, long>");
    }
}

TEST_CASE("a null handler restores the default", "[contract]")
{
    const auto previous = jetio::set_contract_violation_handler(&some_handler);
    const auto mine = jetio::set_contract_violation_handler(nullptr);
    CHECK(mine == &some_handler);
    const auto restored = jetio::set_contract_violation_handler(previous);
    CHECK(restored != nullptr);
    CHECK(restored != &some_handler);
}

TEST_CASE("contracts work in constant expressions", "[contract]")
{
    constexpr auto check = [](int v)
    {
        JIO_Expects(v > 0);
        JIO_Expects_critical(v < 100);
        return v;
    };
    static_assert(check(1) == 1);
    CHECK(check(2) == 2);
}
