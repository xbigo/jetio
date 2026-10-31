#include <jetio/jetio.hpp>

#include <array>
#include <cstddef>
#include <limits>
#include <new>
#include <numeric>
#include <vector>

#include "support/contract_guard.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace jetio;
using jetio::test::contract_failure;

namespace
{
    std::vector<std::byte> bytes(std::size_t n, int first = 0)
    {
        std::vector<std::byte> v(n);
        for (std::size_t i = 0; i < n; ++i)
            v[i] = static_cast<std::byte>((first + static_cast<int>(i)) & 0xff);
        return v;
    }
} // namespace

TEST_CASE("memory_device models the protocol", "[memory][concepts]")
{
    using vec = memory_device<>;
    static_assert(reader<vec> && writer<vec> && syncer<vec> && is_eofer<vec> && sizer<vec> &&
                  truncater<vec> && sequence<vec> && random_access<vec> && forward_access<vec> &&
                  read_map<vec> && write_map<vec>);

    using fixed = memory_device<mutable_buffer_represent>;
    static_assert(reader<fixed> && writer<fixed> && random_access<fixed> && read_map<fixed> &&
                  write_map<fixed>);
    static_assert(!truncater<fixed>);

    using readonly = memory_device<const_buffer_represent>;
    static_assert(reader<readonly> && random_access<readonly> && read_map<readonly>);
    static_assert(!writer<readonly> && !truncater<readonly> && !write_map<readonly> &&
                  !syncer<readonly>);
}

TEST_CASE("a new vector device is empty", "[memory]")
{
    memory_device<> device;
    CHECK(device.size() == 0);
    CHECK(device.offset() == 0);
    CHECK(device.is_eof());
}

TEST_CASE("write grows the device, read returns the data", "[memory]")
{
    memory_device<> device;
    const auto data = bytes(10);

    CHECK(write(device, data).empty());
    CHECK(device.offset() == 10);
    CHECK(device.size() == 10);
    CHECK(device.is_eof());
    CHECK(device.underlying().data == data);

    CHECK(seek(device, 0) == 0);
    CHECK(!device.is_eof());
    std::vector<std::byte> out(4);
    auto got = read(device, out);
    CHECK(got.size() == 4);
    CHECK(got.data() == out.data());
    CHECK(std::equal(out.begin(), out.end(), data.begin()));
    CHECK(device.offset() == 4);
}

TEST_CASE("a short read means the end of the data", "[memory]")
{
    memory_device<> device(vector_buffer_represent{bytes(6), 4});
    std::vector<std::byte> out(10);
    CHECK(read(device, out).size() == 2);
    CHECK(device.is_eof());
    CHECK(read(device, out).empty());
    CHECK(device.offset() == 6);

    CHECK(read(device, mutable_buffer{}).empty());
}

TEST_CASE("reading and writing beyond the end", "[memory]")
{
    memory_device<> device(vector_buffer_represent{bytes(4), 0});

    CHECK(seek(device, 100) == 100);
    std::vector<std::byte> out(3);
    CHECK(read(device, out).empty());
    CHECK(device.offset() == 100);

    const auto data = bytes(2, 0x40);
    CHECK(write(device, data).empty());
    CHECK(device.size() == 102);
    // the gap reads as zero
    const auto &stored = device.underlying().data;
    CHECK(stored[50] == std::byte{0});
    CHECK(stored[100] == std::byte{0x40});
    CHECK(stored[101] == std::byte{0x41});
}

TEST_CASE("writing nothing changes nothing", "[memory]")
{
    memory_device<> device;
    CHECK(seek(device, 5) == 5);
    CHECK(write(device, const_buffer{}).empty());
    CHECK(device.size() == 0);
    CHECK(device.offset() == 5);
}

// GCC 16 reports an out-of-bounds memset in the dead growth path of std::vector::resize when
// the sizes are compile-time constants and the optimizer inlines everything (-O3).
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif
TEST_CASE("truncate sets the size, new space is zero", "[memory]")
{
    memory_device<> device(vector_buffer_represent{bytes(4, 1), 2});
    CHECK(truncate(device, 6) == 6);
    CHECK(device.size() == 6);
    CHECK(device.offset() == 2);
    CHECK(device.underlying().data[3] == std::byte{4});
    CHECK(device.underlying().data[5] == std::byte{0});

    CHECK(truncate(device, 1) == 1);
    CHECK(device.size() == 1);
    CHECK(device.offset() == 2);
    CHECK(device.is_eof());
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

TEST_CASE("views", "[memory]")
{
    memory_device<> device(vector_buffer_represent{bytes(20), 5});

    SECTION("read view")
    {
        auto view = view_rd(device, {2, 7});
        CHECK(view.address().size() == 5);
        CHECK(view.address().data() == device.underlying().data.data() + 2);
        CHECK(device.offset() == 5);

        CHECK(view_rd(device, {15, unknown_offset}).address().size() == 5);
        CHECK(view_rd(device, {20, unknown_offset}).address().empty());
        CHECK(view_rd(device, {0, 20}).address().size() == 20);
    }

    SECTION("read view beyond the end is an error")
    {
        error_code ec;
        CHECK(view_rd(device, {0, 21}, &ec).address().empty());
        CHECK(ec == std::errc::invalid_argument);
        CHECK(view_rd(device, {21, unknown_offset}, &ec).address().empty());
        CHECK(ec == std::errc::invalid_argument);
        view_rd(device, {0, 20}, &ec);
        CHECK(!ec);

        try
        {
            view_rd(device, {0, 21});
            FAIL("no exception");
        }
        catch (const io_error &e)
        {
            CHECK(e.code() == std::errc::invalid_argument);
        }
    }

    SECTION("write view grows the device")
    {
        auto view = view_wr(device, {10, 100});
        CHECK(view.address().size() == 90);
        CHECK(device.size() == 100);
        CHECK(device.offset() == 5);
        view.address()[0] = std::byte{0x7f};
        CHECK(device.underlying().data[10] == std::byte{0x7f});
        CHECK(view_wr(device, {0, unknown_offset}).address().size() == 100);
    }

    SECTION("write view with unknown begin beyond the end is an error")
    {
        error_code ec;
        view_wr(device, {30, unknown_offset}, &ec);
        CHECK(ec == std::errc::invalid_argument);
        CHECK(device.size() == 20);
    }

    SECTION("an invalid range violates the contract")
    {
        if constexpr (contract_level >= 1)
        {
            test::scoped_throwing_contract_handler handler;
            CHECK_THROWS_AS(view_rd(device, {5, 2}), contract_failure);
            CHECK_THROWS_AS(view_wr(device, {-1, 2}), contract_failure);
            CHECK_THROWS_AS(device.view_rd({5, 2}), contract_failure);
        }
    }
}

TEST_CASE("an error code is cleared on success", "[memory]")
{
    memory_device<> device;
    error_code ec = std::make_error_code(std::errc::io_error);
    CHECK(write(device, bytes(1), &ec).empty());
    CHECK(!ec);

    ec = std::make_error_code(std::errc::io_error);
    CHECK(device.size(&ec) == 1);
    CHECK(!ec);
}

TEST_CASE("fixed size writable memory", "[memory]")
{
    std::array<std::byte, 8> storage{};
    mutable_buffer buffer(storage);
    std::size_t pos = 0;
    memory_device<mutable_buffer_represent> device(mutable_buffer_represent{buffer, pos});

    SECTION("a write that fits")
    {
        CHECK(write(device, bytes(8)).empty());
        CHECK(storage[7] == std::byte{7});
        CHECK(pos == 8);
    }

    SECTION("a write that does not fit is cut and reported")
    {
        seek(device, 5);
        const auto data = bytes(6, 1);
        error_code ec;
        const auto rest = write(device, data, &ec);
        CHECK(ec == std::errc::no_space_on_device);
        CHECK(rest.size() == 3);
        CHECK(rest.data() == data.data() + 3);
        CHECK(pos == 8);
        CHECK(storage[5] == std::byte{1});
        CHECK(storage[7] == std::byte{3});
        CHECK(storage[4] == std::byte{0});

        CHECK_THROWS_AS(write(device, data), io_error);
    }

    SECTION("a write beyond the end writes nothing")
    {
        seek(device, 20);
        error_code ec;
        CHECK(write(device, bytes(1), &ec).size() == 1);
        CHECK(ec == std::errc::no_space_on_device);
        CHECK(pos == 20);
    }

    SECTION("a write view cannot grow the memory")
    {
        error_code ec;
        CHECK(view_wr(device, {0, 9}, &ec).address().empty());
        CHECK(ec == std::errc::no_space_on_device);
        CHECK(view_wr(device, {2, 8}, &ec).address().size() == 6);
        CHECK(!ec);
    }
}

TEST_CASE("read-only memory", "[memory]")
{
    const auto data = bytes(5, 10);
    const_buffer buffer(data);
    std::size_t pos = 0;
    memory_device<const_buffer_represent> device(const_buffer_represent{buffer, pos});

    std::vector<std::byte> out(8);
    CHECK(read(device, out).size() == 5);
    CHECK(out[0] == std::byte{10});
    CHECK(device.is_eof());
    CHECK(seek(device, 1) == 1);
    CHECK(view_rd(device, {1, 3}).address().data() == data.data() + 1);
}

namespace
{
    struct accessor_represent
    {
        std::vector<std::byte> bytes;
        std::size_t position = 0;

        std::vector<std::byte> &data() { return bytes; }
        const std::vector<std::byte> &data() const { return bytes; }
        std::size_t pos() const { return position; }
        void pos(std::size_t p) { position = p; }
    };
} // namespace

TEST_CASE("a represent with accessors", "[memory]")
{
    static_assert(truncatable_represent<accessor_represent>);
    memory_device<accessor_represent> device;
    CHECK(write(device, bytes(3)).empty());
    CHECK(device.underlying().bytes.size() == 3);
    CHECK(device.underlying().position == 3);
}

TEST_CASE("seek_forward", "[memory]")
{
    memory_device<> device(vector_buffer_represent{bytes(10), 4});
    CHECK(seek_forward(device, 7) == 7);
    CHECK(seek_forward(device, 7) == 7);
    CHECK(device.offset() == 7);

    if constexpr (contract_level >= 2)
    {
        test::scoped_throwing_contract_handler handler;
        CHECK_THROWS_AS(device.seek_forward(3), contract_failure);
        if constexpr (contract_level >= 3)
            CHECK_THROWS_AS(seek_forward(device, 3), contract_failure);
    }
}

namespace
{
    // Fails to allocate more than `limit` bytes at once.
    template <typename T> struct limited_allocator
    {
        using value_type = T;
        static inline std::size_t limit = std::numeric_limits<std::size_t>::max();

        limited_allocator() = default;
        template <typename U> limited_allocator(const limited_allocator<U> &) noexcept {}

        T *allocate(std::size_t n)
        {
            if (n > limit)
                throw std::bad_alloc();
            return std::allocator<T>().allocate(n);
        }
        void deallocate(T *p, std::size_t n) noexcept { std::allocator<T>().deallocate(p, n); }

        friend bool operator==(const limited_allocator &, const limited_allocator &) noexcept
        {
            return true;
        }
    };

    struct limited_represent
    {
        std::vector<std::byte, limited_allocator<std::byte>> data;
        std::size_t pos{0};
    };
} // namespace

TEST_CASE("an end that does not fit in size_t is an error", "[memory]")
{
    memory_device<> device;
    error_code ec;

    seek(device, std::numeric_limits<std::size_t>::max() - 1);
    CHECK(write(device, bytes(4), &ec).size() == 4);
    CHECK(ec == std::errc::value_too_large);
    CHECK(device.size() == 0);
    CHECK(device.offset() == std::numeric_limits<std::size_t>::max() - 1);
    CHECK_THROWS_AS(write(device, bytes(4)), io_error);
}

TEST_CASE("a size beyond the maximum of the container is an error", "[memory]")
{
    memory_device<> device(vector_buffer_represent{bytes(3), 1});
    const size64_t too_large = size64_t{1} << 63;
    error_code ec;

    CHECK(truncate(device, too_large, &ec) == 3);
    CHECK(ec == std::errc::value_too_large);
    CHECK_THROWS_AS(truncate(device, too_large), io_error);

    // nothing changed
    CHECK(device.size() == 3);
    CHECK(device.offset() == 1);
}

TEST_CASE("failing to allocate leaves the device unchanged", "[memory]")
{
    using device_type = memory_device<limited_represent>;
    static_assert(truncater<device_type> && write_map<device_type>);

    device_type device;
    CHECK(write(device, bytes(4)).empty());
    limited_allocator<std::byte>::limit = 8;
    const auto restore = [] { limited_allocator<std::byte>::limit = ~std::size_t{0}; };

    error_code ec;
    CHECK(truncate(device, 100, &ec) == 4);
    CHECK(ec == std::errc::not_enough_memory);
    CHECK(device.size() == 4);

    CHECK(write(device, bytes(100), &ec).size() == 100);
    CHECK(ec == std::errc::not_enough_memory);
    CHECK(device.size() == 4);
    CHECK(device.offset() == 4);

    CHECK(view_wr(device, {0, 100}, &ec).address().empty());
    CHECK(ec == std::errc::not_enough_memory);
    CHECK(device.size() == 4);

    try
    {
        write(device, bytes(100));
        FAIL("no exception");
    }
    catch (const io_error &e)
    {
        CHECK(e.code() == std::errc::not_enough_memory);
    }
    CHECK(device.size() == 4);

    restore();
    CHECK(write(device, bytes(100)).empty());
    CHECK(device.size() == 104);
}
