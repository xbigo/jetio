#include <jetio/jetio.hpp>

#include <array>
#include <vector>

#include "support/contract_guard.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace jetio;

TEST_CASE("pseudo devices model the protocol", "[pseudo][concepts]")
{
    static_assert(reader<zero_device> && random_access<zero_device> && is_eofer<zero_device> &&
                  sizer<zero_device> && !writer<zero_device>);
    static_assert(reader<fill_device> && random_access<fill_device>);
    static_assert(writer<null_device> && syncer<null_device> && truncater<null_device> &&
                  random_access<null_device> && !reader<null_device>);
    static_assert(reader<empty_device> && random_access<empty_device> && !writer<empty_device>);
}

TEST_CASE("zero_device reads zeros", "[pseudo]")
{
    zero_device device;
    std::vector<std::byte> out(5, std::byte{0xaa});
    CHECK(read(device, out).size() == 5);
    CHECK(out == std::vector<std::byte>(5, std::byte{0}));
    CHECK(device.offset() == 5);
    CHECK(!device.is_eof());
    CHECK(device.size() == unknown_size);
    CHECK(seek(device, 100) == 100);
    CHECK(device.offset() == 100);
}

TEST_CASE("fill_device repeats its pattern", "[pseudo]")
{
    const std::array<std::byte, 3> pattern{std::byte{1}, std::byte{2}, std::byte{3}};
    fill_device device(pattern);

    std::vector<std::byte> out(8);
    CHECK(read(device, out).size() == 8);
    const std::vector<std::byte> expected{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{1},
                                          std::byte{2}, std::byte{3}, std::byte{1}, std::byte{2}};
    CHECK(out == expected);
    CHECK(device.offset() == 8);

    // continues where it stopped
    out.resize(2);
    read(device, out);
    CHECK(out == std::vector<std::byte>{std::byte{3}, std::byte{1}});

    // any offset, any buffer size
    for (size64_t start = 0; start < 7; ++start)
    {
        for (std::size_t n = 0; n < 10; ++n)
        {
            seek(device, start);
            out.assign(n, std::byte{0xff});
            read(device, out);
            for (std::size_t i = 0; i < n; ++i)
                CHECK(out[i] == pattern[(start + i) % 3]);
        }
    }
}

TEST_CASE("fill_device needs a pattern", "[pseudo]")
{
    if constexpr (contract_level >= 1)
    {
        test::throwing_contract_handler handler;
        CHECK_THROWS_AS(fill_device(const_buffer{}), test::contract_failure);
    }
}

TEST_CASE("null_device discards", "[pseudo]")
{
    null_device device;
    const std::array<std::byte, 4> data{};
    CHECK(write(device, data).empty());
    CHECK(device.offset() == 4);
    CHECK(device.size() == 0);
    CHECK(device.is_eof());

    CHECK(truncate(device, 0) == 0);
    error_code ec;
    CHECK(truncate(device, 5, &ec) == 0);
    CHECK(ec == std::errc::invalid_argument);
    CHECK_THROWS_AS(truncate(device, 5), io_error);
    sync(device);
}

TEST_CASE("empty_device has no data", "[pseudo]")
{
    empty_device device;
    std::vector<std::byte> out(4);
    CHECK(read(device, out).empty());
    CHECK(seek(device, 9) == 0);
    CHECK(device.offset() == 0);
    CHECK(device.is_eof());
    CHECK(device.size() == 0);
}
