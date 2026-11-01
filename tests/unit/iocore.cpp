#include <jetio/jetio.hpp>

#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace jetio;

namespace
{
    // Only the operations of a plain stream.
    struct stream_device
    {
        size64_t position = 0;
        size64_t offset(error_code_ptr) const { return position; }
        size64_t seek_forward(size64_t off, error_code_ptr)
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

TEST_CASE("the protocol functions work on a conforming device", "[iocore]")
{
    memory_device<> device;
    std::vector<std::byte> data(16);
    CHECK_NOTHROW(write(device, data));
    seek(device, 0);
    CHECK_NOTHROW(read(device, data));
    CHECK_NOTHROW(view_rd(device, {0, 16}));
}
