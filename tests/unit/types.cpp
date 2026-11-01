#include <jetio/types.hpp>

#include <limits>

#include <catch2/catch_test_macros.hpp>

using namespace jetio;

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
    SUCCEED();
}

TEST_CASE("conversion to platform types", "[types]")
{
    static_assert(in_size_t_range(size64_t{0}));
    static_assert(in_size_t_range(offset64_t{0}));
    static_assert(!in_size_t_range(offset64_t{-1}));
    static_assert(to_size_t(42) == 42);
    static_assert(offset_to_size_t(42) == 42);
    static_assert(to_ptrdiff_t(-42) == -42);

    // where size_t has 32 bits, a 64 bit size can be too large
    static_assert(sizeof(std::size_t) != 4 || !in_size_t_range(size64_t{1} << 32));
    SUCCEED();
}
