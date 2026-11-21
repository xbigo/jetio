#include <jio/std/string_view.hpp>
#include <jio/string_algo/uri.hpp>

#include <string>

#include <catch2/catch_test_macros.hpp>

using namespace jio;

namespace
{
    std::string encoded(const auto &in)
    {
        std::string out;
        uri::encode_string(in, out);
        return out;
    }

    std::string form_encoded(const auto &in)
    {
        std::string out;
        uri::form_encode_string(in, out);
        return out;
    }

    std::string decoded(const auto &in, error_code_ptr ec = nullptr)
    {
        std::string out;
        uri::decode_string(in, out, ec);
        return out;
    }

    std::string form_decoded(const auto &in)
    {
        std::string out;
        uri::form_decode_string(in, out);
        return out;
    }
} // namespace

TEST_CASE("bytes are percent encoded and back", "[uri]")
{
    const std::string source = "abc 123\t";
    const std::string text = encoded(source);
    CHECK(text == "abc%20123%09");
    CHECK(decoded(text) == source);

    // what is not unreserved, the bytes of UTF-8 too
    CHECK(encoded("a-b_c.d~e/f?g\xc3\xa9"_nt) == "a-b_c.d~e%2Ff%3Fg%C3%A9");
    CHECK(decoded("%e2%82%AC%41"_nt) == "\xe2\x82\xac"
                                        "A");
    CHECK(decoded("a+b"_nt) == "a+b"); // a plus sign is not a space here

    // the text is appended
    std::string out = "?q=";
    uri::encode_string("a b"_nt, out);
    CHECK(out == "?q=a%20b");
}

TEST_CASE("the form variant writes a space as a plus sign", "[uri]")
{
    CHECK(form_encoded("a b!*'()&"_nt) == "a+b!*'()%26");
    CHECK(form_decoded("a+b%26"_nt) == "a b&");
}

TEST_CASE("a bad percent escape is an error", "[uri]")
{
    for (const auto bad : {"%"_nt, "ab%2"_nt, "%zz"_nt, "%2z"_nt})
    {
        error_code ec;
        decoded(bad, &ec);
        CHECK(ec == std::errc::invalid_argument);
    }
    CHECK_THROWS_AS(decoded("%"_nt), uri::invalid_encoded_uri);

    // what was converted before the failure is there
    error_code ec;
    CHECK(decoded("ab%"_nt, &ec) == "ab");
    CHECK(ec);
}
