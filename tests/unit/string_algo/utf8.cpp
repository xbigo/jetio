#include <jio/std/string.hpp>
#include <jio/string_algo/utf8.hpp>

#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace jio;

namespace
{
    std::string encoded(const auto &in, error_code_ptr ec = nullptr)
    {
        std::string out;
        utf8::encode_string(in, out, ec);
        return out;
    }

    template <typename CharT>
    std::basic_string<CharT> decoded(const auto &in, error_code_ptr ec = nullptr)
    {
        std::basic_string<CharT> out;
        utf8::decode_string(in, out, ec);
        return out;
    }
} // namespace

TEST_CASE("code points of every length round trip through UTF-8", "[utf8]")
{
    // one to six bytes, as the general form has
    const std::vector<int> code_points{0x1, 0x81, 0x801, 0x10001, 0x200001, 0x4000001};
    const std::string text = encoded(code_points);
    CHECK(text.size() == 21);

    std::vector<std::uint32_t> back;
    const auto stopped = utf8::decode(text, std::back_inserter(back));
    CHECK(stopped.in == text.end());
    CHECK(back == std::vector<std::uint32_t>(code_points.begin(), code_points.end()));

    CHECK(utf8::encoded_length(0x7f) == 1);
    CHECK(utf8::encoded_length(0x80) == 2);
    CHECK(utf8::encoded_length(0x7ff) == 2);
    CHECK(utf8::encoded_length(0x800) == 3);
    CHECK(utf8::encoded_length(0xffff) == 3);
    CHECK(utf8::encoded_length(0x10000) == 4);
    CHECK(utf8::encoded_length(utf8::max_code_point) == 6);
}

TEST_CASE("UTF-8 text of real characters", "[utf8]")
{
    const std::u32string code_points = U"aé€\U0001f600";
    const std::string text = encoded(code_points);
    CHECK(text == "a\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80");
    CHECK(decoded<char32_t>(text) == code_points);
}

TEST_CASE("wide strings of two bytes are UTF-16", "[utf8]")
{
    const string text = "a\xe2\x82\xac\xf0\x9f\x98\x80"_str; // a, euro, grinning face
    const std::u16string wide = decoded<char16_t>(text);
    CHECK(wide == u"a€\U0001f600"); // the face is a surrogate pair
    CHECK(wide.size() == 4);
    CHECK(encoded(wide) == std::string_view(text));

    // wchar_t is whichever of the two the platform has
    const std::wstring native = decoded<wchar_t>(text);
    CHECK(encoded(native) == std::string_view(text));
}

TEST_CASE("the algorithm stops at the first character that fails and says where", "[utf8]")
{
    const string text = "ab\xe2\x82"_str; // the euro sign cut short
    std::vector<std::uint32_t> out;
    const auto stopped = utf8::decode(text, std::back_inserter(out));
    CHECK(stopped.in == text.begin() + 2);
    CHECK(out == std::vector<std::uint32_t>{'a', 'b'});

    const std::vector<int> code_points{0x61, -1, 0x62};
    std::string bytes;
    const auto encoded = utf8::encode(code_points, std::back_inserter(bytes));
    CHECK(encoded.in == code_points.begin() + 1);
    CHECK(bytes == "a");
}

TEST_CASE("bad UTF-8 is an error, and the string stays valid", "[utf8]")
{
    error_code ec;
    std::u32string out = U"kept";
    utf8::decode_string("ab\xe2\x82"_str, out, &ec); // cut short
    CHECK(ec == std::errc::illegal_byte_sequence);
    CHECK(out.starts_with(U"kept"));
    CHECK_THROWS_AS(utf8::decode_string("ab\xe2\x82"_str, out), utf8::invalid_utf8_code);

    // a continuation byte that does not continue, a continuation byte on its own, 0xff
    for (const auto bad : {"\xe2\x82\x41"_nt, "\x80"_nt, "\xff"_nt})
    {
        utf8::decode_string(bad, out, &ec);
        CHECK(ec == std::errc::illegal_byte_sequence);
    }

    out = U"kept";
    utf8::decode_string("fine"_nt, out, &ec);
    CHECK(!ec);                // cleared on success
    CHECK(out == U"keptfine"); // the result is appended
}

TEST_CASE("a value that is not a code point is an error", "[utf8]")
{
    error_code ec;
    std::string out = "kept";
    utf8::encode_string(std::vector<std::uint32_t>{0x80000000u}, out, &ec);
    CHECK(ec == std::errc::result_out_of_range);
    utf8::encode_string(std::vector<int>{-1}, out, &ec);
    CHECK(ec == std::errc::result_out_of_range);
    utf8::encode_string(std::u16string{u'\xd800'}, out, &ec); // a lone surrogate
    CHECK(ec == std::errc::result_out_of_range);
    CHECK_THROWS_AS(utf8::encode_string(std::vector<int>{-1}, out), utf8::unicode_out_range);
    CHECK(out.starts_with("kept"));

    // what UTF-16 cannot hold
    std::u16string wide;
    utf8::decode_string(encoded(std::vector<int>{0x110000}), wide, &ec);
    CHECK(ec == std::errc::result_out_of_range);
}

TEST_CASE("the exceptions say where they were thrown", "[utf8]")
{
    try
    {
        (void)decoded<char32_t>("\xff"_nt);
        FAIL("no exception");
    }
    catch (const utf8::invalid_utf8_code &e)
    {
        CHECK(e.code() == std::errc::illegal_byte_sequence);
        CHECK(e.where().line() != 0);
    }
}
