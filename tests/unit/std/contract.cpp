// White-box tests of the contract facility. A violated contract cannot be continued, so the
// handler throws and the tests catch the exception.
#include <jio/std/contract.hpp>

#include <optional>
#include <type_traits>

#include "support/contract_guard.hpp"
#include <catch2/catch_test_macros.hpp>

using jio::contract_kind;
using jio::test::contract_failure;

namespace
{
    constexpr bool checked(int min_level)
    {
        return jio::contract_level >= min_level;
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

    // The macro in `f` has a false condition: it must reach the handler exactly if the level
    // reaches min_level, and report the kind of contract.
    template <typename F> void expect_checked_from(int min_level, contract_kind kind, F &&f)
    {
        const auto failure = violation_of(f);
        CHECK(failure.has_value() == checked(min_level));
        if (failure.has_value())
            CHECK(failure.value().kind == kind);
    }

    [[noreturn]] void some_handler(const jio::contract_violation &v)
    {
        throw contract_failure(v);
    }
} // namespace

TEST_CASE("a satisfied contract is silent", "[contract]")
{
    jio::test::scoped_throwing_contract_handler handler;
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
    jio::test::scoped_throwing_contract_handler handler;

    expect_checked_from(1, contract_kind::precondition, [] { JIO_Expects_critical(false); });
    expect_checked_from(1, contract_kind::assertion, [] { JIO_Assert_critical(false); });

    expect_checked_from(2, contract_kind::precondition, [] { JIO_Expects(false); });
    expect_checked_from(2, contract_kind::postcondition, [] { JIO_Ensures(false); });
    expect_checked_from(2, contract_kind::assertion, [] { JIO_Assert(false); });

    expect_checked_from(3, contract_kind::precondition, [] { JIO_Expects_audit(false); });
    expect_checked_from(3, contract_kind::postcondition, [] { JIO_Ensures_audit(false); });
    expect_checked_from(3, contract_kind::assertion, [] { JIO_Assert_audit(false); });
}

TEST_CASE("a condition may contain commas", "[contract]")
{
    jio::test::scoped_throwing_contract_handler handler;
    CHECK_NOTHROW(JIO_Expects(std::is_same_v<int, int>));
    CHECK(violation_of([] { JIO_Expects(std::is_same_v<int, long>); }).has_value() == checked(2));
}

TEST_CASE("a null handler restores the default", "[contract]")
{
    const auto previous = jio::set_contract_violation_handler(&some_handler);
    const auto mine = jio::set_contract_violation_handler(nullptr);
    CHECK(mine == &some_handler);
    const auto restored = jio::set_contract_violation_handler(previous);
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
