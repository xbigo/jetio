#include <jio/std/string_view.hpp>
#include <jio/string_algo/hex.hpp>

#include <array>
#include <cstddef>
#include <iterator>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace jio;

namespace
{
    std::string hexed(const auto &in, hex::letter_case letters = hex::letter_case::lower)
    {
        std::string out;
        hex::encode_string(in, out, letters);
        return out;
    }

    std::vector<std::byte> unhexed(const auto &in, error_code_ptr ec = nullptr)
    {
        std::vector<std::byte> out;
        hex::decode_bytes(in, out, ec);
        return out;
    }
} // namespace

TEST_CASE("bytes are hexadecimal text and back", "[hex]")
{
    const std::array<std::byte, 4> bytes{std::byte{0x00}, std::byte{0x1f}, std::byte{0xab},
                                         std::byte{0xff}};
    CHECK(hexed(bytes) == "001fabff");
    CHECK(hexed(bytes, hex::letter_case::upper) == "001FABFF");
    CHECK(hexed("Hi"_nt) == "4869");

    CHECK(unhexed("001fABff"_nt) == std::vector<std::byte>(bytes.begin(), bytes.end()));
    CHECK(unhexed(""_nt).empty());

    // the text is appended
    std::string out = "0x";
    hex::encode_string(bytes, out);
    CHECK(out == "0x001fabff");
}

TEST_CASE("the algorithm stops at the first thing that is not a pair of digits", "[hex]")
{
    // the digit that has no partner is left out ...
    const std::string odd = "0102f";
    std::vector<std::byte> written;
    const auto stopped = hex::decode(odd, std::back_inserter(written));
    CHECK(stopped.in == odd.begin() + 4);
    CHECK(written == std::vector<std::byte>{std::byte{1}, std::byte{2}});

    // ... and a bad digit stops at its pair, after the ones before it
    written.clear();
    const std::string bad = "0102zz04";
    const auto at = hex::decode(bad, std::back_inserter(written));
    CHECK(at.in == bad.begin() + 4);
    CHECK(written == std::vector<std::byte>{std::byte{1}, std::byte{2}});
}

TEST_CASE("bad hexadecimal text is an error", "[hex]")
{
    error_code ec;
    unhexed("abc"_nt, &ec); // odd
    CHECK(ec == std::errc::invalid_argument);
    unhexed("zz"_nt, &ec);
    CHECK(ec == std::errc::invalid_argument);
    CHECK_THROWS_AS(unhexed("g0"_nt), hex::bad_hex_input);

    // what was converted before the failure is there
    CHECK(unhexed("0102zz"_nt, &ec) == std::vector<std::byte>{std::byte{1}, std::byte{2}});
    CHECK(ec);

    CHECK(unhexed("00"_nt, &ec).size() == 1);
    CHECK(!ec);
}
