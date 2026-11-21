#include <jio/std/string_view.hpp>
#include <jio/string_algo/string.hpp>

#include <algorithm>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

using namespace jio;
using namespace jio::str_algo;

namespace
{
    std::string replaced(const auto &text, char from, char to)
    {
        std::string out;
        replace(text, from, to, out);
        return out;
    }

    std::string replaced(const auto &text, const auto &pattern, const auto &with)
    {
        std::string out;
        replace(text, pattern, with, out);
        return out;
    }

    std::string upper(const auto &text)
    {
        std::string out;
        ascii_to_upper(text, out);
        return out;
    }

    std::string lower(const auto &text)
    {
        std::string out;
        ascii_to_lower(text, out);
        return out;
    }
} // namespace

TEST_CASE("replace puts a character or a pattern in place of another", "[str_algo]")
{
    CHECK(replaced("This is a test"_nt, 'i', 'I') == "ThIs Is a test");

    const auto text = "this is a mac, and that is a mac, and here is a mac."_nt;
    CHECK(replaced(text, "mac"_nt, "pc"_nt) == "this is a pc, and that is a pc, and here is a pc.");

    // what was put in is not searched again, and the replacement may be empty or longer
    CHECK(replaced("aaa"_nt, "aa"_nt, "a"_nt) == "aa");
    CHECK(replaced("abcabc"_nt, "b"_nt, ""_nt) == "acac");
    CHECK(replaced("a.b"_nt, "."_nt, "<->"_nt) == "a<->b");
    CHECK(replaced(std::string("xyz"), "w"_nt, "!"_nt) == "xyz");
    CHECK(replaced(""_nt, "a"_nt, "b"_nt).empty());

    // the result is appended
    std::string out = "> ";
    replace("a-b"_nt, '-', '+', out);
    CHECK(out == "> a+b");
}

TEST_CASE("replace_copy is the algorithm under replace", "[str_algo]")
{
    std::string out = "> ";
    const auto end = replace_copy("one two one"_nt, "one"_nt, "1"_nt, std::back_inserter(out));
    (void)end;
    CHECK(out == "> 1 two 1");
}

TEST_CASE("the case of ASCII letters changes, other bytes stay", "[str_algo]")
{
    static_assert(ascii_to_upper('a') == 'A' && ascii_to_upper('z') == 'Z' &&
                  ascii_to_lower('A') == 'a' &&
                  ascii_to_lower('Z') == 'z'); // tables at compile time
    CHECK(ascii_to_upper('i') == 'I');
    CHECK(ascii_to_upper('I') == 'I');
    CHECK(ascii_to_upper('$') == '$');
    CHECK(ascii_to_lower('I') == 'i');
    CHECK(ascii_to_lower('i') == 'i');
    CHECK(ascii_to_upper('\xe9') == '\xe9'); // bytes of other characters, negative as char
    CHECK(ascii_to_lower('\0') == '\0');

    CHECK(lower("THis Is -- A TesT."_nt) == "this is -- a test.");
    CHECK(upper("THis Is -- A TesT."_nt) == "THIS IS -- A TEST.");
    CHECK(upper("\xc3\xa9t\xc3\xa9"_nt) == "\xc3\xa9T\xc3\xa9"); // the bytes of é stay
}

TEST_CASE("the case insensitive functors serve the standard algorithms", "[str_algo]")
{
    CHECK(icase_equal{}('a', 'A'));
    CHECK(!icase_equal{}('a', 'b'));
    CHECK(icase_less{}('A', 'b'));
    CHECK(!icase_less{}('b', 'A'));
    CHECK(icase_compare{}('A', 'b') < 0);
    CHECK(icase_compare{}('A', 'a') == 0);
    CHECK(icase_compare{}('B', 'a') > 0);

    const std::string_view a("Content-Type");
    const std::string_view b("content-TYPE");
    CHECK(std::equal(a.begin(), a.end(), b.begin(), b.end(), icase_equal{}));
    const std::string_view d("d");
    CHECK(std::lexicographical_compare(a.begin(), a.end(), d.begin(), d.end(), icase_less{}));
}
