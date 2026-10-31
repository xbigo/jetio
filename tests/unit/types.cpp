#include <jetio/types.hpp>

#include <limits>

#include "support/contract_guard.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace jetio;
using jetio::test::contract_failure;

TEST_CASE("valid ranges", "[types]")
{
    static_assert(is_valid_range({0, 0}));
    static_assert(is_valid_range({3, 10}));
    static_assert(is_valid_range({3, unknown_offset}));
    static_assert(!is_valid_range({-1, 10}));
    static_assert(!is_valid_range({5, 4}));
    static_assert(!is_valid_range({0, -1}));
    static_assert(!is_valid_range({unknown_offset, 0}));

    static_assert(is_valid_range({0, 10}, 10));
    static_assert(!is_valid_range({0, 11}, 10));
    static_assert(is_valid_range({0, unknown_offset}, 10));
    static_assert(!is_valid_range({11, 11}, 10));
}

TEST_CASE("range size", "[types]")
{
    static_assert(range_size({3, 10}) == 7);
    static_assert(range_size({3, 3}) == 0);
    static_assert(range_size({3, unknown_offset}) == unknown_size);
    static_assert(unknown_size == std::numeric_limits<offset64_t>::max());

    if constexpr (contract_level >= 2)
    {
        test::scoped_throwing_contract_handler handler;
        CHECK_THROWS_AS(range_size({5, 4}), contract_failure);
        CHECK_THROWS_AS(range_size({-1, 4}), contract_failure);
    }
}

TEST_CASE("conversion to platform types", "[types]")
{
    static_assert(in_size_t_range(size64_t{0}));
    static_assert(in_size_t_range(offset64_t{0}));
    static_assert(!in_size_t_range(offset64_t{-1}));
    static_assert(to_size_t(42) == 42);
    static_assert(offset_to_size_t(42) == 42);
    static_assert(to_ptrdiff_t(-42) == -42);

    if constexpr (sizeof(std::size_t) == 4)
    {
        CHECK(!in_size_t_range(size64_t{1} << 32));
    }
    if constexpr (contract_level >= 1 && sizeof(std::size_t) == 4)
    {
        test::scoped_throwing_contract_handler handler;
        CHECK_THROWS_AS(to_size_t(size64_t{1} << 32), contract_failure);
    }
}
