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

    /// Gives at most two bytes per read, as a stream may.
    class trickle_device
    {
    public:
        explicit trickle_device(std::size_t n) : m_device(device_of(n)) {}
        mutable_buffer read(mutable_buffer buf, error_code_ptr ec = nullptr)
        {
            return jetio::read(m_device, buf.first(std::min<std::size_t>(buf.size(), 2)), ec);
        }
        size64_t offset(error_code_ptr ec = nullptr) const { return m_device.offset(ec); }
        size64_t seek_forward(size64_t off, error_code_ptr ec = nullptr)
        {
            return m_device.seek_forward(off, ec);
        }
        size64_t size(error_code_ptr ec = nullptr) const { return m_device.size(ec); }

    private:
        memory_device<> m_device;
    };
} // namespace

TEST_CASE("read_full fills the buffer from a device that reads less", "[cache_view]")
{
    trickle_device device(7);
    std::vector<std::byte> out(5);
    CHECK(jetio::read(device, out).size() == 2); // read alone gives what the device gives
    CHECK(offset(device) == 2);

    CHECK(read_full(device, out).size() == 5);
    CHECK(out == bytes(5, 2));
    CHECK(offset(device) == 7);
}

TEST_CASE("read_full stops at the end of the data", "[cache_view]")
{
    trickle_device device(5);
    std::vector<std::byte> out(10);
    CHECK(read_full(device, out).size() == 5);
    CHECK(read_full(device, out).empty());
    CHECK(read_full(device, mutable_buffer{}).empty());
}

TEST_CASE("cache views model the protocol", "[cache_view][concepts]")
{
    using vec = memory_device<>;
    static_assert(read_map<reader_to_view<vec>> && write_map<writer_to_view<vec>>);
    static_assert(read_map<reader_to_view<trickle_device>>);
    static_assert(read_view<cache_rd_view> && write_view<cache_wr_view<vec>>);
    static_assert(!std::is_copy_constructible_v<cache_wr_view<vec>> &&
                  std::is_move_constructible_v<cache_wr_view<vec>>);
}

TEST_CASE("reader_to_view reads the range into memory", "[cache_view]")
{
    auto device = device_of(10);
    reader_to_view readable(device);

    const cache_rd_view view = view_rd(readable, {2, 6});
    CHECK(std::vector<std::byte>(view.address().begin(), view.address().end()) == bytes(4, 2));

    // the end of the data is the unknown end
    const cache_rd_view tail = view_rd(readable, {7, unknown_offset});
    CHECK(tail.address().size() == 3);
    CHECK(view_rd(readable, {10, 10}).address().empty());
    CHECK(readable.size() == 10);
}

TEST_CASE("reader_to_view reports a range beyond the data", "[cache_view]")
{
    auto device = device_of(10);
    reader_to_view readable(device);

    error_code ec;
    CHECK(view_rd(readable, {8, 12}, &ec).address().empty());
    CHECK(ec == std::errc::invalid_argument);
    CHECK_THROWS_AS(view_rd(readable, {8, 12}), io_error);
    CHECK(view_rd(readable, {11, unknown_offset}, &ec).address().empty());
    CHECK(ec == std::errc::invalid_argument);
}

TEST_CASE("reader_to_view works on a device that reads less and only goes forward", "[cache_view]")
{
    trickle_device device(10);
    reader_to_view readable(device);
    const cache_rd_view view = view_rd(readable, {3, 8});
    CHECK(std::vector<std::byte>(view.address().begin(), view.address().end()) == bytes(5, 3));
}

TEST_CASE("writer_to_view writes the bytes when the view is destroyed", "[cache_view]")
{
    auto device = device_of(10);
    writer_to_view writable(device);

    error_code ec;
    {
        auto view = view_wr(writable, {2, 5}, &ec);
        CHECK(!ec);
        CHECK(address(view).size() == 3);
        CHECK(address(view)[0] == std::byte{0}); // the bytes of a new view are zero
        address(view)[0] = std::byte{100};
        address(view)[2] = std::byte{102};
    }
    CHECK(!ec);

    seek(device, 2);
    std::vector<std::byte> out(3);
    jetio::read(device, out);
    CHECK(out[0] == std::byte{100});
    CHECK(out[1] == std::byte{0});
    CHECK(out[2] == std::byte{102});
}

TEST_CASE("a view may end beyond the data and grow the device", "[cache_view]")
{
    auto device = device_of(10);
    writer_to_view writable(device);
    {
        auto view = view_wr(writable, {4, 6});
        address(view)[1] = std::byte{77};
    }
    seek(device, 5);
    std::vector<std::byte> out(1);
    jetio::read(device, out);
    CHECK(out[0] == std::byte{77});

    {
        auto view = view_wr(writable, {12, 14});
        address(view)[0] = std::byte{1};
    }
    CHECK(device.size() == 14);
}

namespace
{
    // A device that cannot hold what the view writes.
    struct small_device
    {
        std::array<std::byte, 4> storage{};
        mutable_buffer buffer{storage};
        std::size_t pos = 0;
        memory_device<mutable_buffer_represent> device{mutable_buffer_represent{buffer, pos}};
    };
} // namespace

TEST_CASE("the destructor reports a failure in the error code the view was given", "[cache_view]")
{
    small_device small;
    writer_to_view writable(small.device);
    error_code ec;
    {
        auto view = view_wr(writable, {0, 6}, &ec);
        CHECK(!ec);
    }
    CHECK(ec == std::errc::no_space_on_device);
}

TEST_CASE("the destructor sets and clears the error code like any operation", "[cache_view]")
{
    // a failure replaces what the error code holds
    small_device small;
    writer_to_view writable(small.device);
    error_code ec;
    {
        auto view = view_wr(writable, {0, 6}, &ec);
        ec = std::make_error_code(std::errc::io_error);
    }
    CHECK(ec == std::errc::no_space_on_device);

    // and a success clears it
    auto device = device_of(4);
    writer_to_view fine(device);
    {
        auto view = view_wr(fine, {0, 2}, &ec);
        ec = std::make_error_code(std::errc::io_error);
    }
    CHECK(!ec);
}

TEST_CASE("the destructor without an error code drops a failure and does not throw", "[cache_view]")
{
    small_device small;
    writer_to_view writable(small.device);
    CHECK_NOTHROW([&] { auto view = view_wr(writable, {0, 6}); }());
}

namespace
{
    /// Writes, but does not know its size.
    class nosize_device
    {
    public:
        const_buffer write(const_buffer buf, error_code_ptr ec = nullptr)
        {
            return jetio::write(m_device, buf, ec);
        }
        size64_t offset(error_code_ptr ec = nullptr) const { return m_device.offset(ec); }
        size64_t seek(size64_t off, error_code_ptr ec = nullptr) { return m_device.seek(off, ec); }

    private:
        memory_device<> m_device = device_of(8);
    };
} // namespace

namespace
{
    /// Takes at most two bytes per write, as a socket may.
    class short_write_device
    {
    public:
        const_buffer write(const_buffer buf, error_code_ptr ec = nullptr)
        {
            const std::size_t n = std::min<std::size_t>(buf.size(), 2);
            const const_buffer rest = jetio::write(m_device, buf.first(n), ec);
            return buf.last(buf.size() - n + rest.size());
        }
        size64_t offset(error_code_ptr ec = nullptr) const { return m_device.offset(ec); }
        size64_t seek(size64_t off, error_code_ptr ec = nullptr) { return m_device.seek(off, ec); }
        memory_device<> &memory() { return m_device; }

    private:
        memory_device<> m_device = device_of(8);
    };
} // namespace

TEST_CASE("the destructor writes everything to a device that takes less per write", "[cache_view]")
{
    short_write_device device;
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
    CHECK(jetio::read(device.memory(), out).size() == 5);
    CHECK(out == bytes(5, 50));
}

TEST_CASE("an unknown end needs a device that knows its size", "[cache_view]")
{
    nosize_device device;
    writer_to_view writable(device);
    static_assert(!sizer<writer_to_view<nosize_device>>);

    error_code ec;
    {
        auto view = view_wr(writable, {0, unknown_offset}, &ec);
        CHECK(address(view).empty());
    }
    CHECK(ec == std::errc::invalid_argument);
    CHECK_THROWS_AS(view_wr(writable, {0, unknown_offset}), io_error);
}

TEST_CASE("a moved view writes once", "[cache_view]")
{
    auto device = device_of(4);
    writer_to_view writable(device);
    {
        auto view = view_wr(writable, {0, 2});
        address(view)[0] = std::byte{9};
        auto moved = std::move(view);
        CHECK(address(moved).size() == 2);
    }
    seek(device, 0);
    std::vector<std::byte> out(1);
    jetio::read(device, out);
    CHECK(out[0] == std::byte{9});
}
