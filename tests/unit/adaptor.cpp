#include <jetio/jetio.hpp>

#include <array>
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

    memory_device<> device_of(std::size_t n)
    {
        return memory_device<>(vector_buffer_represent{bytes(n), 0});
    }

    /// A stream: reads and moves forward only.
    class forward_only
    {
    public:
        explicit forward_only(std::size_t n) : m_device(device_of(n)) {}
        size64_t offset(error_code_ptr ec = nullptr) const { return m_device.offset(ec); }
        size64_t seek_forward(size64_t off, error_code_ptr ec = nullptr)
        {
            return m_device.seek_forward(off, ec);
        }
        mutable_buffer read(mutable_buffer buf, error_code_ptr ec = nullptr)
        {
            return m_device.read(buf, ec);
        }

    private:
        memory_device<> m_device;
    };

    std::vector<std::byte> read_n(auto &device, std::size_t n)
    {
        std::vector<std::byte> out(n);
        out.resize(read(device, out).size());
        return out;
    }
} // namespace

TEST_CASE("adaptors provide what the wrapped device provides", "[adaptor][concepts]")
{
    using vec = memory_device<>;
    static_assert(random_access<multiplex_device<vec>> && reader<multiplex_device<vec>> &&
                  writer<multiplex_device<vec>> && is_eofer<multiplex_device<vec>> &&
                  sizer<multiplex_device<vec>> && truncater<multiplex_device<vec>> &&
                  read_map<multiplex_device<vec>> && write_map<multiplex_device<vec>>);
    static_assert(forward_access<shift_device<vec>> && random_access<shift_device<vec>> &&
                  reader<shift_device<vec>> && writer<shift_device<vec>> &&
                  read_map<shift_device<vec>> && write_map<shift_device<vec>>);
    static_assert(random_access<sub_device<vec>> && reader<sub_device<vec>> &&
                  writer<sub_device<vec>> && is_eofer<sub_device<vec>> &&
                  sizer<sub_device<vec>> && truncater<sub_device<vec>> &&
                  read_map<sub_device<vec>> && write_map<sub_device<vec>>);

    using readonly = memory_device<const_buffer_represent>;
    static_assert(reader<multiplex_device<readonly>> && !writer<multiplex_device<readonly>> &&
                  !truncater<multiplex_device<readonly>> && !write_map<multiplex_device<readonly>>);
    static_assert(reader<shift_device<readonly>> && !writer<shift_device<readonly>> &&
                  !write_map<shift_device<readonly>>);
    static_assert(reader<sub_device<readonly>> && !writer<sub_device<readonly>> &&
                  !syncer<sub_device<readonly>> && !write_map<sub_device<readonly>>);

    // a device that cannot go back is still shifted, but the result cannot go back either
    static_assert(forward_access<shift_device<forward_only>> &&
                  !random_access<shift_device<forward_only>> && reader<shift_device<forward_only>>);
    static_assert(!random_access<forward_only>);
}

TEST_CASE("multiplex_device gives each user an own offset", "[adaptor][multiplex]")
{
    auto device = device_of(10);
    multiplex_device a(device);
    multiplex_device b(device, 5);

    CHECK(read_n(a, 3) == bytes(3, 0));
    CHECK(read_n(b, 3) == bytes(3, 5));
    CHECK(read_n(a, 3) == bytes(3, 3));
    CHECK(a.offset() == 6);
    CHECK(b.offset() == 8);

    CHECK(seek(a, 1) == 1);
    CHECK(read_n(a, 2) == bytes(2, 1));
    CHECK(read_n(b, 5) == bytes(2, 8)); // up to the end of the data
    CHECK(b.is_eof());
    CHECK(!a.is_eof());
    CHECK(a.size() == 10);
}

TEST_CASE("multiplex_device writes where its own offset is", "[adaptor][multiplex]")
{
    auto device = device_of(6);
    multiplex_device a(device);
    multiplex_device b(device, 4);

    const auto data = bytes(2, 100);
    CHECK(write(a, data).empty());
    CHECK(write(b, data).empty());
    CHECK(write(a, data).empty());
    CHECK(a.offset() == 4);
    CHECK(b.offset() == 6);

    seek(a, 0);
    const std::vector<std::byte> expected{std::byte{100}, std::byte{101}, std::byte{100},
                                          std::byte{101}, std::byte{100}, std::byte{101}};
    CHECK(read_n(a, 10) == expected);

    CHECK(truncate(a, 3) == 3);
    CHECK(a.size() == 3);
    sync(a);
    CHECK(address(view_rd(a, {1, 3})).size() == 2);
}

TEST_CASE("shift_device hides the first bytes", "[adaptor][shift]")
{
    auto device = device_of(10);
    shift_device shifted(device, 4);

    CHECK(shifted.shift() == 4);
    CHECK(shifted.offset() == 0);
    CHECK(shifted.size() == 6);
    CHECK(read_n(shifted, 3) == bytes(3, 4)); // moved forward to the shift first
    CHECK(shifted.offset() == 3);
    CHECK(device.offset() == 7);

    CHECK(seek(shifted, 0) == 0);
    CHECK(device.offset() == 4);
    CHECK(read_n(shifted, 100) == bytes(6, 4));
    CHECK(shifted.is_eof());

    seek(shifted, 0);
    CHECK(seek_forward(shifted, 2) == 2);
    CHECK(read_n(shifted, 1) == bytes(1, 6));
}

TEST_CASE("shift_device skips the header of a stream", "[adaptor][shift]")
{
    forward_only stream(10);
    shift_device shifted(stream, 4);
    CHECK(shifted.offset() == 0);
    CHECK(read_n(shifted, 2) == bytes(2, 4));
    CHECK(shifted.offset() == 2);
    CHECK(seek_forward(shifted, 5) == 5);
    CHECK(read_n(shifted, 10) == bytes(1, 9));
}

TEST_CASE("shift_device is_eof looks at what is left behind the shift", "[adaptor][shift]")
{
    auto device = device_of(3);
    shift_device shifted(device, 5);
    CHECK(shifted.size() == 0);
    CHECK(shifted.is_eof()); // the device is behind the shift, and empty
    CHECK(read_n(shifted, 4).empty());
}

TEST_CASE("shift_device moves views, writes and truncation by the shift", "[adaptor][shift]")
{
    auto device = device_of(10);
    shift_device shifted(device, 4);

    CHECK(address(view_rd(shifted, {1, 3})).size() == 2);
    CHECK(address(view_rd(shifted, {1, 3})).data() == address(view_rd(device, {5, 7})).data());
    CHECK(address(view_rd(shifted, {2, unknown_offset})).size() == 4); // to the end of the data

    const auto data = bytes(2, 200);
    seek(shifted, 1);
    CHECK(write(shifted, data).empty());
    CHECK(read_n(device, 0).empty());
    seek(device, 5);
    CHECK(read_n(device, 2) == data);

    CHECK(truncate(shifted, 3) == 3);
    CHECK(device.size() == 7);
    CHECK(shifted.size() == 3);
}

TEST_CASE("shift_device reports what does not fit", "[adaptor][shift]")
{
    auto device = device_of(10);
    shift_device shifted(device, 4);
    const size64_t huge = std::numeric_limits<size64_t>::max() - 2;

    error_code ec;
    CHECK(seek(shifted, huge, &ec) == 0);
    CHECK(ec == std::errc::value_too_large);
    CHECK_THROWS_AS(seek(shifted, huge), io_error);
    CHECK(truncate(shifted, huge, &ec) == 0);
    CHECK(ec == std::errc::value_too_large);

    const offset64_t far = unknown_offset - 2;
    CHECK(address(view_rd(shifted, {far, unknown_offset}, &ec)).empty());
    CHECK(ec == std::errc::value_too_large);
    CHECK(address(view_wr(shifted, {0, far}, &ec)).empty());
    CHECK(ec == std::errc::value_too_large);

    // nothing changed
    CHECK(read_n(shifted, 2) == bytes(2, 4));
}

TEST_CASE("sub_device is a window with offsets from 0", "[adaptor][sub]")
{
    auto device = device_of(10);
    sub_device window(device, {2, 7});

    CHECK(window.section() == offset64_range{2, 7});
    CHECK(window.size() == 5);
    CHECK(window.offset() == 0);
    CHECK(read_n(window, 3) == bytes(3, 2));
    CHECK(window.offset() == 3);
    CHECK(read_n(window, 10) == bytes(2, 5)); // ends at the end of the window
    CHECK(window.is_eof());
    CHECK(read_n(window, 1).empty());

    CHECK(seek(window, 1) == 1);
    CHECK(!window.is_eof());
    CHECK(read_n(window, 1) == bytes(1, 3));

    // beyond the end: allowed, nothing to read
    CHECK(seek(window, 100) == 100);
    CHECK(window.offset() == 100);
    CHECK(window.is_eof());
    CHECK(read_n(window, 1).empty());
}

TEST_CASE("windows share a device and keep their own offsets", "[adaptor][sub]")
{
    auto device = device_of(10);
    sub_device first(device, {0, 4});
    sub_device second(device, {4, 10});

    CHECK(read_n(second, 2) == bytes(2, 4));
    CHECK(read_n(first, 2) == bytes(2, 0));
    CHECK(read_n(second, 2) == bytes(2, 6));
    CHECK(first.offset() == 2);
    CHECK(second.offset() == 4);
}

TEST_CASE("a write to a window stops at its end", "[adaptor][sub]")
{
    auto device = device_of(10);
    sub_device window(device, {2, 5});

    const auto data = bytes(5, 100);
    seek(window, 1);
    error_code ec;
    const const_buffer rest = write(window, data, &ec);
    CHECK(ec == std::errc::no_space_on_device);
    CHECK(rest.size() == 3); // two bytes fit
    CHECK(rest.data() == data.data() + 2);
    CHECK(window.offset() == 3);

    seek(device, 2);
    const std::vector<std::byte> expected{std::byte{2}, std::byte{100}, std::byte{101},
                                          std::byte{5}};
    CHECK(read_n(device, 4) == expected);

    // at the end nothing fits; writing nothing is fine
    CHECK(write(window, data, &ec).size() == 5);
    CHECK(ec == std::errc::no_space_on_device);
    CHECK_THROWS_AS(write(window, data), io_error);
    CHECK(write(window, const_buffer{}, &ec).empty());
    CHECK(!ec);
}

TEST_CASE("views of a window are relative to it", "[adaptor][sub]")
{
    auto device = device_of(10);
    sub_device window(device, {2, 7});

    CHECK(address(view_rd(window, {1, 3})).data() == address(view_rd(device, {3, 5})).data());
    CHECK(address(view_rd(window, {1, unknown_offset})).size() == 4);
    CHECK(address(view_wr(window, {0, 5})).size() == 5);
    CHECK(address(view_rd(window, {5, 5})).empty());

    error_code ec;
    CHECK(address(view_rd(window, {0, 6}, &ec)).empty());
    CHECK(ec == std::errc::invalid_argument);
    CHECK(address(view_wr(window, {6, unknown_offset}, &ec)).empty());
    CHECK(ec == std::errc::invalid_argument);
    CHECK_THROWS_AS(view_rd(window, {0, 6}), io_error);
}

TEST_CASE("a window cannot be resized", "[adaptor][sub]")
{
    auto device = device_of(10);
    sub_device window(device, {2, 7});

    CHECK(truncate(window, 5) == 5);
    error_code ec;
    CHECK(truncate(window, 4, &ec) == 5);
    CHECK(ec == std::errc::invalid_argument);
    CHECK_THROWS_AS(truncate(window, 6), io_error);
    sync(window);
    CHECK(device.size() == 10);
}

TEST_CASE("a window over a shorter device reads less", "[adaptor][sub]")
{
    auto device = device_of(4);
    sub_device window(device, {2, 8});
    CHECK(window.size() == 6);
    CHECK(read_n(window, 10) == bytes(2, 2));
}
