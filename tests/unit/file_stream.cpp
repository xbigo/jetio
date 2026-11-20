#include <jio/jio.hpp>

#include <array>
#include <atomic>
#include <filesystem>
#include <random>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace jio;

namespace
{
    namespace fs = std::filesystem;

    /// A name in the temporary directory, unique to the process and the test; the file, if any,
    /// is removed with the object.
    class temp_file
    {
    public:
        temp_file()
        {
            static std::atomic<int> counter{0};
            static const unsigned run = std::random_device{}();
            m_path = fs::temp_directory_path() /
                     ("jetio-test-" + std::to_string(run) + "-" + std::to_string(counter++));
        }
        temp_file(const temp_file &) = delete;
        temp_file &operator=(const temp_file &) = delete;
        ~temp_file()
        {
            std::error_code ignored;
            fs::remove(m_path, ignored);
        }
        const path &get() const { return m_path; }

    private:
        path m_path;
    };

    std::vector<std::byte> bytes(std::size_t n, int first = 0)
    {
        std::vector<std::byte> v(n);
        for (std::size_t i = 0; i < n; ++i)
            v[i] = static_cast<std::byte>((first + static_cast<int>(i)) & 0xff);
        return v;
    }

    void put(const path &file, const std::vector<std::byte> &data)
    {
        file_writer writer(file, open_mode::empty);
        CHECK(jio::write(writer, data).empty());
    }
} // namespace

TEST_CASE("file devices model the protocol", "[file_stream][concepts]")
{
    static_assert(reader<file_reader> && is_eofer<file_reader> && sizer<file_reader> &&
                  random_access<file_reader> && forward_access<file_reader> &&
                  !writer<file_reader> && !truncater<file_reader> && !syncer<file_reader>);
    static_assert(writer<file_writer> && syncer<file_writer> && truncater<file_writer> &&
                  sizer<file_writer> && random_access<file_writer> && !reader<file_writer> &&
                  !is_eofer<file_writer>);
    static_assert(reader<file_device> && writer<file_device> && is_eofer<file_device> &&
                  syncer<file_device> && truncater<file_device> && sizer<file_device> &&
                  random_access<file_device>);
    static_assert(std::is_move_constructible_v<file_device> &&
                  !std::is_copy_constructible_v<file_device>);
}

TEST_CASE("a writer makes the file and a reader reads it back", "[file_stream]")
{
    temp_file file;
    const auto data = bytes(10);
    {
        file_writer writer(file.get(), open_mode::create_new);
        CHECK(writer.size() == 0);
        CHECK(jio::write(writer, data).empty());
        CHECK(offset(writer) == 10);
        CHECK(writer.size() == 10);
        jio::sync(writer);
    }

    file_reader reader(file.get());
    CHECK(reader.size() == 10);
    CHECK(!reader.is_eof());
    std::vector<std::byte> out(4);
    CHECK(jio::read(reader, out).size() == 4);
    CHECK(out == bytes(4));
    CHECK(offset(reader) == 4);

    // a short buffer is not the end, a long one gets what is there
    out.resize(20);
    CHECK(jio::read(reader, out).size() == 6);
    CHECK(reader.is_eof());
    CHECK(jio::read(reader, out).empty());
}

TEST_CASE("seeking is free and the data follows the offset", "[file_stream]")
{
    temp_file file;
    put(file.get(), bytes(10));
    file_reader reader(file.get());

    CHECK(seek(reader, 7) == 7);
    std::vector<std::byte> out(5);
    CHECK(jio::read(reader, out).size() == 3);
    CHECK(out[0] == std::byte{7});

    CHECK(seek(reader, 2) == 2);
    CHECK(jio::read(reader, mutable_buffer(out).first(2)).size() == 2);
    CHECK(out[1] == std::byte{3});

    CHECK(seek_forward(reader, 9) == 9);
    CHECK(jio::read(reader, out).size() == 1);

    // beyond the end: no data, no error
    error_code ec;
    CHECK(seek(reader, 1000, &ec) == 1000);
    CHECK(!ec);
    CHECK(reader.is_eof());
    CHECK(jio::read(reader, out, &ec).empty());
    CHECK(!ec);
}

TEST_CASE("devices on the same file keep their own offsets", "[file_stream]")
{
    temp_file file;
    put(file.get(), bytes(10));
    file_reader a(file.get());
    file_reader b(file.get());
    std::vector<std::byte> out(3);
    jio::read(a, out);
    CHECK(offset(a) == 3);
    CHECK(offset(b) == 0);
    jio::read(b, out);
    CHECK(out == bytes(3));
    CHECK(offset(a) == 3);
}

TEST_CASE("writing beyond the end grows the file with zeros", "[file_stream]")
{
    temp_file file;
    file_device device(file.get(), open_mode::create);
    seek(device, 4);
    const auto data = bytes(2, 100);
    CHECK(jio::write(device, data).empty());
    CHECK(device.size() == 6);

    seek(device, 0);
    std::vector<std::byte> out(6);
    CHECK(jio::read(device, out).size() == 6);
    CHECK(out == std::vector<std::byte>{std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
                                        std::byte{100}, std::byte{101}});
}

TEST_CASE("an empty buffer is no work", "[file_stream]")
{
    temp_file file;
    file_device device(file.get(), open_mode::create);
    CHECK(jio::write(device, const_buffer{}).empty());
    CHECK(jio::read(device, mutable_buffer{}).empty());
    CHECK(device.size() == 0);
}

TEST_CASE("truncate sets the size and keeps the offset", "[file_stream]")
{
    temp_file file;
    file_device device(file.get(), open_mode::empty);
    jio::write(device, bytes(10));

    CHECK(jio::truncate(device, 4) == 4);
    CHECK(device.size() == 4);
    CHECK(offset(device) == 10);

    CHECK(jio::truncate(device, 8) == 8); // new space reads as zero
    seek(device, 0);
    std::vector<std::byte> out(8);
    CHECK(jio::read(device, out).size() == 8);
    CHECK(out[3] == std::byte{3});
    CHECK(out[4] == std::byte{0});
    CHECK(out[7] == std::byte{0});

    // too large for the file system: an error, and the size is unchanged
    error_code ec;
    CHECK(jio::truncate(device, size64_t{1} << 63, &ec) == 8);
    CHECK(ec == std::errc::file_too_large);
    CHECK_THROWS_AS(jio::truncate(device, size64_t{1} << 63), io_error);
}

TEST_CASE("open modes", "[file_stream]")
{
    temp_file file;
    error_code ec;

    SECTION("existing needs the file")
    {
        file_device missing(file.get(), open_mode::existing, &ec);
        CHECK(ec == std::errc::no_such_file_or_directory);
        CHECK_THROWS_AS(file_device(file.get(), open_mode::existing), io_error);
        CHECK_THROWS_AS(file_reader(file.get()), io_error);
        file_reader missing_reader(file.get(), &ec);
        CHECK(ec == std::errc::no_such_file_or_directory);
    }

    SECTION("create_new needs no file")
    {
        file_writer first(file.get(), open_mode::create_new, &ec);
        CHECK(!ec);
        file_writer second(file.get(), open_mode::create_new, &ec);
        CHECK(ec == std::errc::file_exists);
    }

    SECTION("create keeps the data, empty drops it")
    {
        put(file.get(), bytes(5));
        {
            file_device kept(file.get(), open_mode::create);
            CHECK(kept.size() == 5);
        }
        {
            file_device existing(file.get(), open_mode::existing);
            CHECK(existing.size() == 5);
        }
        {
            file_device emptied(file.get(), open_mode::empty);
            CHECK(emptied.size() == 0);
        }
    }

    SECTION("a directory is not a place to write")
    {
        file_writer dir(fs::temp_directory_path(), open_mode::existing, &ec);
        CHECK(ec == std::errc::is_a_directory);
    }
}

TEST_CASE("a moved device is the same device", "[file_stream]")
{
    temp_file file;
    file_device first(file.get(), open_mode::create);
    jio::write(first, bytes(3));

    file_device second(std::move(first));
    CHECK(offset(second) == 3);
    CHECK(second.size() == 3);

    file_device third(file.get(), open_mode::create);
    third = std::move(second);
    CHECK(offset(third) == 3);
}

TEST_CASE("the constructor's error code is cleared on success and by the destructor",
          "[file_stream]")
{
    temp_file file;
    error_code ec = std::make_error_code(std::errc::io_error);
    {
        file_device device(file.get(), open_mode::create, &ec);
        CHECK(!ec);
        ec = std::make_error_code(std::errc::io_error);
    }
    CHECK(!ec); // the close succeeded

    // a failed open leaves nothing to close
    file_device missing(file.get() / "no" / "such", open_mode::existing, &ec);
    CHECK(ec);
}

#ifdef __linux__
TEST_CASE("a write that fails reports it and writes nothing", "[file_stream]")
{
    file_writer full("/dev/full", open_mode::existing);
    const auto data = bytes(4);
    error_code ec;
    CHECK(jio::write(full, data, &ec).size() == 4);
    CHECK(ec == std::errc::no_space_on_device);
    CHECK(offset(full) == 0);
    CHECK_THROWS_AS(jio::write(full, data), io_error);
}
#endif
