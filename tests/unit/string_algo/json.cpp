#include <jio/std/string_view.hpp>
#include <jio/string_algo/json.hpp>

#include <string>

#include <catch2/catch_test_macros.hpp>

using namespace jio;

namespace
{
    std::string quoted(const auto &in)
    {
        std::string out;
        json::escape_string(in, out);
        return out;
    }
} // namespace

TEST_CASE("text is escaped for a JSON string", "[json]")
{
    CHECK(quoted("plain"_nt) == "\"plain\"");
    CHECK(quoted("say \"hi\" \\ bye"_nt) == "\"say \\\"hi\\\" \\\\ bye\"");
    CHECK(quoted("a\nb\t\x01"_nt) == "\"a\\u000ab\\u0009\\u0001\"");
    CHECK(quoted("\xc3\xa9"_nt) == "\"\xc3\xa9\""); // UTF-8 is kept
    CHECK(quoted(""_nt) == "\"\"");

    std::string out;
    json::escape(std::string("a\"b"), std::back_inserter(out));
    CHECK(out == "a\\\"b");
}
