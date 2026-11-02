#include <jetio/jetio.hpp>

#include <cstddef>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace jetio;

namespace
{
    std::vector<std::byte> bytes(std::size_t n, int first = 0)
    {
        std::vector<std::byte> v(n);
        for (std::size_t i = 0; i < n; ++i)
            v[i] = static_cast<std::byte>((first + static_cast<int>(i)) & 0xff);
        return v;
    }

    /// A policy that judges one operation only.
    struct no_sync
    {
        error_code sync() { return std::make_error_code(std::errc::io_error); }
    };
} // namespace

TEST_CASE("bad_device has the capabilities of a memory device", "[bad_device][concepts]")
{
    using device = bad_device<bad::at_most>;
    static_assert(reader<device> && writer<device> && random_access<device> &&
                  forward_access<device> && sizer<device> && is_eofer<device> &&
                  truncater<device> && syncer<device> && read_map<device> && write_map<device>);
}

TEST_CASE("at_most makes reads and writes short", "[bad_device]")
{
    bad_device device(bad::at_most(2), bytes(6));

    std::vector<std::byte> out(5);
    CHECK(read(device, out).size() == 2);
    CHECK(device.offset() == 2);
    CHECK(read(device, mutable_buffer{}).empty());

    const auto data = bytes(5, 100);
    const const_buffer rest = write(device, data);
    CHECK(rest.size() == 3);
    CHECK(rest.data() == data.data() + 2);
    CHECK(device.offset() == 4);
    seek(device.memory(), 2);
    CHECK(read(device.memory(), mutable_buffer{out}.first(2)).size() == 2);
    CHECK(out[0] == std::byte{100});
    CHECK(out[1] == std::byte{101});
}

TEST_CASE("read_full and a write loop get through short transfers", "[bad_device]")
{
    bad_device device(bad::at_most(1), bytes(4));
    std::vector<std::byte> out(4);
    CHECK(read_full(device, out).size() == 4);
    CHECK(out == bytes(4));
}

TEST_CASE("after_bytes lets a number of bytes through and then fails", "[bad_device]")
{
    bad_device device(bad::after_bytes(5), bytes(10));
    std::vector<std::byte> out(3);
    CHECK(read(device, out).size() == 3);

    // the call that goes beyond the limit transfers the rest
    out.resize(4);
    CHECK(read(device, out).size() == 2);

    error_code ec;
    CHECK(read(device, out, &ec).empty());
    CHECK(ec == std::errc::io_error);
    CHECK(device.offset() == 5);
    CHECK_THROWS_AS(read(device, out), io_error);

    // the device stays usable, and what is not asked is not refused
    CHECK(seek(device, 0, &ec) == 0);
    CHECK(!ec);
    CHECK(device.size() == 10);
}

TEST_CASE("after_bytes shortens a write and then fails with the whole buffer", "[bad_device]")
{
    bad_device device(bad::after_bytes(3, std::make_error_code(std::errc::no_space_on_device)));
    const auto data = bytes(4);

    error_code ec;
    CHECK(write(device, data, &ec).size() == 1);
    CHECK(!ec);
    CHECK(device.size() == 3);

    const const_buffer rest = write(device, const_buffer{data}.last(1), &ec);
    CHECK(rest.size() == 1);
    CHECK(ec == std::errc::no_space_on_device);
    CHECK(device.size() == 3);
}

TEST_CASE("on_call fails one call, once", "[bad_device]")
{
    bad_device device(bad::on_call(2), bytes(4));
    std::vector<std::byte> out(2);
    error_code ec;

    CHECK(read(device, out, &ec).size() == 2);
    CHECK(!ec);
    CHECK(seek(device, 0, &ec) == 2); // the offset did not move
    CHECK(ec == std::errc::io_error);
    CHECK(seek(device, 0, &ec) == 0);
    CHECK(!ec);
    CHECK(read(device, out, &ec).size() == 2);
}

TEST_CASE("a policy judges the operations it has a function for", "[bad_device]")
{
    bad_device device(no_sync{}, bytes(4));
    std::vector<std::byte> out(2);
    CHECK(read(device, out).size() == 2);
    error_code ec;
    sync(device, &ec);
    CHECK(ec == std::errc::io_error);
    CHECK_THROWS_AS(sync(device), io_error);
}

TEST_CASE("views and truncation can fail too", "[bad_device]")
{
    struct policy
    {
        error_code view_rd(offset64_range &) { return std::make_error_code(std::errc::io_error); }
        error_code truncate(size64_t &) { return std::make_error_code(std::errc::io_error); }
    };
    bad_device device(policy{}, bytes(4));
    error_code ec;
    CHECK(view_rd(device, {0, 2}, &ec).address().empty());
    CHECK(ec == std::errc::io_error);
    CHECK(truncate(device, 1, &ec) == 4);
    CHECK(ec == std::errc::io_error);
    CHECK(view_wr(device, {0, 2}, &ec).address().size() == 2);
    CHECK(!ec);
}

TEST_CASE("a write view over a device that takes less writes everything", "[bad_device]")
{
    bad_device device(bad::at_most(2), bytes(8));
    writer_to_view writable(device);
    error_code ec;
    {
        auto view = view_wr(writable, {1, 6}, &ec);
        for (std::size_t i = 0; i < 5; ++i)
            address(view)[i] = static_cast<std::byte>(50 + i);
    }
    CHECK(!ec);
    seek(device.memory(), 1);
    std::vector<std::byte> out(5);
    CHECK(read(device.memory(), out).size() == 5);
    CHECK(out == bytes(5, 50));
}

TEST_CASE("a write view reports the failure of the device", "[bad_device]")
{
    bad_device device(bad::after_bytes(3), bytes(8));
    writer_to_view writable(device);
    error_code ec;
    {
        auto view = view_wr(writable, {0, 6}, &ec);
        CHECK(!ec);
    }
    CHECK(ec == std::errc::io_error);
}
