#include <jetio/jetio.hpp>

#include <algorithm>
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
    CHECK_NOTHROW(jetio::write(device, data));
    seek(device, 0);
    CHECK_NOTHROW(jetio::read(device, data));
    CHECK_NOTHROW(view_rd(device, {0, 16}));
}

// Devices written outside namespace jetio, with free functions found by ADL.
namespace custom
{
    struct free_device
    {
        std::vector<std::byte> data = std::vector<std::byte>(4, std::byte{7});
        size64_t pos = 0;
    };

    jetio::mutable_buffer read(free_device &device, jetio::mutable_buffer buf,
                               jetio::error_code_ptr ec)
    {
        jetio::clear_error(ec);
        const auto count = std::min(buf.size(), device.data.size() - device.pos);
        std::copy_n(device.data.begin() + static_cast<std::ptrdiff_t>(device.pos), count,
                    buf.begin());
        device.pos += count;
        return buf.first(count);
    }

    jetio::size64_t offset(const free_device &device, jetio::error_code_ptr ec)
    {
        jetio::clear_error(ec);
        return device.pos;
    }

    jetio::size64_t seek(free_device &device, jetio::size64_t off, jetio::error_code_ptr ec)
    {
        jetio::clear_error(ec);
        return device.pos = off;
    }

    // Both: the free function is used.
    struct both_device
    {
        jetio::mutable_buffer read(jetio::mutable_buffer buf, jetio::error_code_ptr)
        {
            return buf.first(0);
        }
        bool used_free = false;
    };
    jetio::mutable_buffer read(both_device &device, jetio::mutable_buffer buf,
                               jetio::error_code_ptr)
    {
        device.used_free = true;
        return buf;
    }

    // A function of the same name that is not an operation of the protocol is not one.
    struct unrelated_device
    {
    };
    int read(unrelated_device &, void *, unsigned long)
    {
        return 0;
    }
} // namespace custom

TEST_CASE("a device can provide its operations as free functions", "[iocore][adl]")
{
    static_assert(reader<custom::free_device> && sequence<custom::free_device> &&
                  random_access<custom::free_device> && forward_access<custom::free_device>);
    static_assert(!writer<custom::free_device> && !sizer<custom::free_device>);
    static_assert(!reader<custom::unrelated_device>);

    custom::free_device device;
    std::vector<std::byte> out(3);
    CHECK(jetio::read(device, out).size() == 3); // the default error_code_ptr is supplied
    CHECK(offset(device) == 3);
    CHECK(seek(device, 1) == 1);
    CHECK(seek_forward(device, 2) == 2); // falls back to the free seek
    CHECK(jetio::read(device, out).size() == 2);
}

TEST_CASE("a free function takes precedence over a member", "[iocore][adl]")
{
    static_assert(reader<custom::both_device>);
    custom::both_device device;
    std::vector<std::byte> out(3);
    CHECK(jetio::read(device, out).size() == 3);
    CHECK(device.used_free);
}
