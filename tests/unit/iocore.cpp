#include <jetio/jetio.hpp>

#include <vector>

#include "support/contract_guard.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace jetio;
using jetio::test::contract_failure;

namespace
{
    // Violates the protocol on purpose.
    struct bad_device
    {
        mutable_buffer read(mutable_buffer buf, error_code_ptr) { return buf.last(buf.size() / 2); }
        const_buffer write(const_buffer buf, error_code_ptr) { return buf.first(buf.size() / 2); }
        buffer_rd_view view_rd(long_offset_range, error_code_ptr) { return {}; }
        buffer_wr_view view_wr(long_offset_range, error_code_ptr) { return {}; }
        long_size_t size(error_code_ptr) { return 0; }
    };

    // Only the operations of a plain stream.
    struct stream_device
    {
        long_size_t position = 0;
        long_size_t offset(error_code_ptr) const { return position; }
        long_size_t seek_forward(long_size_t off, error_code_ptr)
        {
            position = off;
            return position;
        }
    };

    struct pointer_to_view
    {
        buffer_rd_view view;
        const buffer_rd_view *operator->() const { return &view; }
    };
} // namespace

TEST_CASE("concepts reject what is not provided", "[iocore][concepts]")
{
    static_assert(!reader<int> && !writer<int> && !sizer<int> && !sequence<int> &&
                  !random_access<int>);
    static_assert(!reader<stream_device> && sequence<stream_device> &&
                  forward_access<stream_device> && !random_access<stream_device>);
    static_assert(read_view<buffer_rd_view> && !write_view<buffer_rd_view>);
    static_assert(read_view<buffer_wr_view> && write_view<buffer_wr_view>);
    static_assert(read_view<pointer_to_view>);
    static_assert(!options<memory_device<>>);
}

TEST_CASE("seek_forward falls back to seek only if the device is random", "[iocore]")
{
    stream_device stream;
    CHECK(seek_forward(stream, 5) == 5);
    CHECK(stream.position == 5);

    memory_device<> memory;
    CHECK(seek_forward(memory, 5) == 5);
}

TEST_CASE("address works for views and for pointers to views", "[iocore]")
{
    std::vector<std::byte> data(3);
    pointer_to_view pointer{buffer_rd_view(data)};
    CHECK(address(pointer).size() == 3);
    CHECK(address(buffer_wr_view(mutable_buffer(data))).data() == data.data());
}

TEST_CASE("the protocol checks the postconditions of a device", "[iocore]")
{
    if constexpr (contract_level < 2)
        SKIP("default contract checks are off");

    test::throwing_contract_handler handler;
    bad_device device;
    std::vector<std::byte> data(4);

    CHECK_THROWS_AS(read(device, data), contract_failure);
    CHECK_THROWS_AS(write(device, data), contract_failure);
    CHECK_THROWS_AS(view_rd(device, {0, 4}), contract_failure);
    CHECK_THROWS_AS(view_wr(device, {0, 4}), contract_failure);
    CHECK_THROWS_AS(view_rd(device, {4, 0}), contract_failure);

    // unknown end: the size of the view is not constrained
    CHECK_NOTHROW(view_rd(device, {0, unknown_offset}));
    // an error excuses the views
    error_code ec = std::make_error_code(std::errc::io_error);
    CHECK_NOTHROW(view_rd(device, {0, 4}, &ec));
}

TEST_CASE("a conforming device passes the checks", "[iocore]")
{
    memory_device<> device;
    std::vector<std::byte> data(16);
    CHECK_NOTHROW(write(device, data));
    seek(device, 0);
    CHECK_NOTHROW(read(device, data));
    CHECK_NOTHROW(view_rd(device, {0, 16}));
}
