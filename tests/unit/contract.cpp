#include <jetio/contract.hpp>

#include <string_view>
#include <type_traits>

#include <catch2/catch_test_macros.hpp>

namespace
{
    int violations = 0;
    jetio::contract_kind last_kind{};
    std::string_view last_expression;
    unsigned last_line = 0;

    void recording_handler(const jetio::contract_violation &v)
    {
        ++violations;
        last_kind = v.kind;
        last_expression = v.expression;
        last_line = v.location.line();
    }

    class recording
    {
    public:
        recording()
        {
            violations = 0;
            m_previous = jetio::set_contract_violation_handler(&recording_handler);
        }
        ~recording() { jetio::set_contract_violation_handler(m_previous); }

    private:
        jetio::contract_violation_handler m_previous;
    };

    constexpr bool checked(int min_level)
    {
        return jetio::contract_level >= min_level;
    }
} // namespace

TEST_CASE("a satisfied contract is silent", "[contract]")
{
    recording rec;
    JIO_Expects(1 + 1 == 2);
    JIO_Ensures(true);
    JIO_Assert(true);
    JIO_Expects_critical(true);
    JIO_Assert_critical(true);
    JIO_Expects_audit(true);
    JIO_Ensures_audit(true);
    JIO_Assert_audit(true);
    CHECK(violations == 0);
}

TEST_CASE("each kind of violation reaches the handler", "[contract]")
{
    if constexpr (!checked(2))
        SKIP("default contract checks are off");

    recording rec;
    SECTION("precondition")
    {
        JIO_Expects(1 > 2);
        CHECK(violations == 1);
        CHECK(last_kind == jetio::contract_kind::precondition);
        CHECK(last_expression == "1 > 2");
        CHECK(last_line == __LINE__ - 4);
    }
    SECTION("postcondition")
    {
        JIO_Ensures(false);
        CHECK(last_kind == jetio::contract_kind::postcondition);
    }
    SECTION("assertion")
    {
        JIO_Assert(false);
        CHECK(last_kind == jetio::contract_kind::assertion);
    }
    SECTION("expression with commas")
    {
        JIO_Expects(std::is_same_v<int, long>);
        CHECK(last_expression == "std::is_same_v<int, long>");
    }
}

// A check is made if the level reaches the level of its macro: critical 1, default 2, audit 3.
TEST_CASE("the level decides which checks are made", "[contract]")
{
    recording rec;
    int evaluated = 0;

    SECTION("critical")
    {
        JIO_Expects_critical(++evaluated < 0);
        JIO_Assert_critical(++evaluated < 0);
        CHECK(evaluated == (checked(1) ? 2 : 0));
        CHECK(violations == (checked(1) ? 2 : 0));
    }
    SECTION("default")
    {
        JIO_Expects(++evaluated < 0);
        JIO_Ensures(++evaluated < 0);
        JIO_Assert(++evaluated < 0);
        CHECK(evaluated == (checked(2) ? 3 : 0));
        CHECK(violations == (checked(2) ? 3 : 0));
    }
    SECTION("audit")
    {
        JIO_Expects_audit(++evaluated < 0);
        JIO_Ensures_audit(++evaluated < 0);
        JIO_Assert_audit(++evaluated < 0);
        CHECK(evaluated == (checked(3) ? 3 : 0));
        CHECK(violations == (checked(3) ? 3 : 0));
    }
}

TEST_CASE("a null handler restores the default", "[contract]")
{
    const auto previous = jetio::set_contract_violation_handler(&recording_handler);
    const auto mine = jetio::set_contract_violation_handler(nullptr);
    CHECK(mine == &recording_handler);
    const auto restored = jetio::set_contract_violation_handler(previous);
    CHECK(restored != nullptr);
    CHECK(restored != &recording_handler);
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
