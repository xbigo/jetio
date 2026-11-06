#include <jio/jio.hpp>

#include <array>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace jio;

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
    CHECK(jio::read(device, out).size() == 5);
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
    CHECK(jio::read(device, out).size() == 8);
    const std::vector<std::byte> expected{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{1},
                                          std::byte{2}, std::byte{3}, std::byte{1}, std::byte{2}};
    CHECK(out == expected);
    CHECK(device.offset() == 8);

    // continues where it stopped
    out.resize(2);
    jio::read(device, out);
    CHECK(out == std::vector<std::byte>{std::byte{3}, std::byte{1}});

    // any offset, any buffer size
    for (offset64_t start = 0; start < 7; ++start)
    {
        for (std::size_t n = 0; n < 10; ++n)
        {
            seek(device, start);
            out.assign(n, std::byte{0xff});
            jio::read(device, out);
            for (std::size_t i = 0; i < n; ++i)
                CHECK(out[i] == pattern[(static_cast<std::size_t>(start) + i) % 3]);
        }
    }
}

TEST_CASE("null_device discards", "[pseudo]")
{
    null_device device;
    const std::array<std::byte, 4> data{};
    CHECK(jio::write(device, data).empty());
    CHECK(device.offset() == 4);
    CHECK(device.size() == 0);
    CHECK(device.is_eof());

    CHECK(jio::truncate(device, 0) == 0);
    error_code ec;
    CHECK(jio::truncate(device, 5, &ec) == 0);
    CHECK(ec == std::errc::invalid_argument);
    CHECK_THROWS_AS(jio::truncate(device, 5), io_error);
    jio::sync(device);
}

TEST_CASE("empty_device has no data", "[pseudo]")
{
    empty_device device;
    std::vector<std::byte> out(4);
    CHECK(jio::read(device, out).empty());
    CHECK(seek(device, 9) == 0);
    CHECK(device.offset() == 0);
    CHECK(device.is_eof());
    CHECK(device.size() == 0);
}

TEST_CASE("generator_device with a positional generator is random access", "[pseudo]")
{
    auto generator = [](offset64_t off) { return static_cast<std::byte>(off * 7); };
    generator_device device(generator);
    static_assert(reader<decltype(device)> && random_access<decltype(device)> &&
                  sizer<decltype(device)> && is_eofer<decltype(device)> &&
                  !writer<decltype(device)>);

    std::vector<std::byte> out(5);
    CHECK(jio::read(device, out).size() == 5);
    CHECK(device.offset() == 5);
    for (std::size_t i = 0; i < 5; ++i)
        CHECK(out[i] == static_cast<std::byte>(i * 7));

    // the same offset gives the same bytes, however it is reached
    std::vector<std::byte> again(3);
    seek(device, 2);
    CHECK(jio::read(device, again).size() == 3);
    CHECK(again[0] == out[2]);
    CHECK(again[2] == out[4]);
    CHECK(device.size() == unknown_size);
    CHECK(!device.is_eof());

    CHECK(device.seek_forward(10) == 10);
    CHECK(device.offset() == 10);
}

TEST_CASE("generator_device with a sequential generator only goes forward", "[pseudo]")
{
    int next = 0;
    generator_device device([&next] { return static_cast<std::byte>(next++); });
    static_assert(reader<decltype(device)> && forward_access<decltype(device)> &&
                  !random_access<decltype(device)>);

    std::vector<std::byte> out(3);
    CHECK(jio::read(device, out).size() == 3);
    CHECK(out == std::vector<std::byte>{std::byte{0}, std::byte{1}, std::byte{2}});

    CHECK(seek_forward(device, 6) == 6);
    CHECK(device.offset() == 6);
    CHECK(jio::read(device, out).size() == 3);
    CHECK(out == std::vector<std::byte>{std::byte{6}, std::byte{7}, std::byte{8}});
    CHECK(seek_forward(device, 9) == 9); // already there
}
