#include <jetio/std/nt_string_view.hpp>
#include <jetio/std/nt_string_view_io.hpp>

#include <array>
#include <format>
#include <functional>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>

#include "support/contract_guard.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace jetio;
using jetio::test::contract_failure;

namespace
{
    // Not ours, with c_str() found by ADL only.
    struct legacy_string
    {
        using value_type = char;
        std::string text;
        operator std::string_view() const noexcept { return text; }
    };
    const char *get_c_str(const legacy_string &s) noexcept
    {
        return s.text.c_str();
    }

    // Has c_str(), but states no terminated end.
    struct liar
    {
        using value_type = char;
        const char *c_str() const noexcept { return "x"; }
        operator std::string_view() const noexcept { return "x"; }
    };

    // Promises a terminator and does not keep the promise.
    struct breaks_its_promise
    {
        using value_type = char;
        const char *c_str() const noexcept { return "abcd"; }
        operator std::string_view() const noexcept { return std::string_view("abcd", 2); }
    };

    template <typename A, typename B>
    concept equality_comparable_between = requires(A a, B b) { a == b; };
    template <typename A, typename B>
    concept orderable_between = requires(A a, B b) { a < b; };
} // namespace

namespace jetio
{
    template <> inline constexpr end_guarantee end_guarantee_of<liar> = end_guarantee::none;
}

TEST_CASE("the guarantee of a type", "[nt_string_view][concepts]")
{
    static_assert(end_guarantee::none < end_guarantee::determinable);
    static_assert(end_guarantee::determinable < end_guarantee::sentinel);
    static_assert(end_guarantee::sentinel < end_guarantee::terminated);

    static_assert(!nd_string_view_like<std::string_view>);
    static_assert(string_view_convertible<std::string_view>);
    static_assert(nt_string_view_like<std::string> && nt_string_view_like<const std::string &>);
    static_assert(nt_string_view_like<nt_string_view> && ns_string_view_like<nt_string_view> &&
                  nd_string_view_like<nt_string_view>);
    static_assert(nt_string_view_like<legacy_string>); // through the ADL get_c_str
    static_assert(!nd_string_view_like<liar>);         // withdrawn by specialization
    static_assert(!nt_string_view_like<const char *>);
    static_assert(nt_string_view_like<basic_nt_string_view<wchar_t>> &&
                  nt_string_view_like<std::wstring>);
    static_assert(std::same_as<char_type_t<std::u8string>, char8_t>);
}

TEST_CASE("get_c_str finds the member or the ADL function", "[nt_string_view]")
{
    const std::string s = "abc";
    CHECK(jetio::get_c_str(s) == s.c_str());
    const legacy_string legacy{"xyz"};
    CHECK(jetio::get_c_str(legacy) == legacy.text.c_str());
}

TEST_CASE("a default view is empty and terminated", "[nt_string_view]")
{
    constexpr nt_string_view view;
    static_assert(view.empty() && view.size() == 0);
    CHECK(view.c_str() != nullptr);
    CHECK(view.c_str()[0] == '\0');
    CHECK(view == std::string_view());
}

TEST_CASE("a view built by scanning stops at the first terminator", "[nt_string_view]")
{
    static constexpr char text[] = "abc\0def";
    constexpr nt_string_view view(text, src_capacity(sizeof(text)));
    static_assert(view.size() == 3);
    CHECK(view == std::string_view("abc"));
    CHECK(view.c_str() == text);

    // the capacity bounds the scan
    const std::array<char, 4> buffer{'a', 'b', 'c', '\0'};
    CHECK(nt_string_view(buffer.data(), src_capacity(4)).size() == 3);
    CHECK(nt_string_view(buffer.data(), src_capacity(10)).size() == 3);
}

TEST_CASE("a view of a known length may contain terminators", "[nt_string_view]")
{
    static constexpr char text[] = "ab\0cd";
    constexpr nt_string_view view(text, 5, src_capacity(6));
    static_assert(view.size() == 5);
    CHECK(view == std::string_view(text, 5));
    CHECK(view[2] == '\0'); // inside the view
    CHECK(view[5] == '\0'); // the terminator
}

TEST_CASE("a view from a type that is terminated", "[nt_string_view]")
{
    const std::string s("a\0b", 3); // the size comes from the string, not from a scan
    const nt_string_view view = s;
    CHECK(view.size() == 3);
    CHECK(view.c_str() == s.c_str());

    const legacy_string legacy{"xyz"};
    const nt_string_view from_legacy = legacy;
    CHECK(from_legacy == std::string_view("xyz"));

    static_assert(std::convertible_to<std::string, nt_string_view>);
    static_assert(!std::convertible_to<std::string_view, nt_string_view>);
    static_assert(!std::convertible_to<const char *, nt_string_view>);
    static_assert(!std::convertible_to<const char (&)[4], nt_string_view>);
    static_assert(!std::convertible_to<liar, nt_string_view>);
    static_assert(std::convertible_to<nt_string_view, std::string_view>);
    static_assert(!std::constructible_from<nt_string_view, std::nullptr_t, src_capacity_t>);
}

TEST_CASE("construction checks the invariant where it can be wrong", "[nt_string_view]")
{
    if constexpr (contract_level < 1)
        SKIP("critical contract checks are off");

    test::scoped_throwing_contract_handler handler;
    const std::array<char, 3> unterminated{'a', 'b', 'c'};
    const std::array<char, 3> terminated{'a', 'b', '\0'};

    // no terminator within the capacity
    CHECK_THROWS_AS(nt_string_view(unterminated.data(), src_capacity(3)), contract_failure);
    CHECK_THROWS_AS(nt_string_view(terminated.data(), src_capacity(0)), contract_failure);
    // a null pointer
    CHECK_THROWS_AS(nt_string_view(static_cast<const char *>(nullptr), src_capacity(1)),
                    contract_failure);
    CHECK_THROWS_AS(nt_string_view(static_cast<const char *>(nullptr), 0, src_capacity(1)),
                    contract_failure);
    // the character at n is not the terminator, or n is not within the capacity
    CHECK_THROWS_AS(nt_string_view(unterminated.data(), 2, src_capacity(3)), contract_failure);
    CHECK_THROWS_AS(nt_string_view(terminated.data(), 2, src_capacity(2)), contract_failure);
    CHECK_NOTHROW(nt_string_view(terminated.data(), 2, src_capacity(3)));

    // a type that does not keep its promise
    CHECK_THROWS_AS(nt_string_view(breaks_its_promise{}), contract_failure);
}

TEST_CASE("element access and the terminator", "[nt_string_view]")
{
    const std::string s = "abc";
    const nt_string_view view = s;
    CHECK(view[0] == 'a');
    CHECK(view[3] == '\0'); // pos == size() is the terminator
    CHECK(view.front() == 'a');
    CHECK(view.back() == 'c');
    CHECK(view.data() == view.c_str());

    if constexpr (contract_level >= 1)
    {
        test::scoped_throwing_contract_handler handler;
        CHECK_THROWS_AS(view[4], contract_failure);
        CHECK_THROWS_AS(nt_string_view().front(), contract_failure);
        CHECK_THROWS_AS(nt_string_view().back(), contract_failure);
    }
}

TEST_CASE("comparison looks at the characters only", "[nt_string_view]")
{
    const std::string a = "abc";
    const std::string b = "abc";
    const std::string longer = "abcd";
    const nt_string_view va = a;
    const nt_string_view vb = b;
    const nt_string_view vlonger = longer;

    CHECK(va.c_str() != vb.c_str()); // different addresses
    CHECK(va == vb);
    CHECK(va != vlonger);
    CHECK(va < vlonger);
    CHECK(va == std::string_view("abc"));
    CHECK(std::string_view("abc") == va);
    CHECK(va == a);
    CHECK(a == va);
    const std::string nothing;
    CHECK(nt_string_view() == std::string_view()); // a static terminator, and a null data()
    CHECK(nt_string_view() == nt_string_view(nothing.c_str(), 0, src_capacity(1)));

    // the terminator is not compared, characters before it are
    static constexpr char with_nul[] = "ab\0c";
    const nt_string_view whole(with_nul, 4, src_capacity(5));
    const nt_string_view scanned(with_nul, src_capacity(5));
    CHECK(whole != scanned);
    CHECK(whole == std::string_view(with_nul, 4));
    CHECK(scanned == std::string_view("ab"));

    // pointers and arrays are not compared
    static_assert(equality_comparable_between<nt_string_view, std::string_view>);
    static_assert(equality_comparable_between<std::string_view, nt_string_view>);
    static_assert(equality_comparable_between<nt_string_view, std::string>);
    static_assert(!equality_comparable_between<nt_string_view, const char *>);
    static_assert(!equality_comparable_between<nt_string_view, const char (&)[4]>);
    static_assert(!equality_comparable_between<const char *, nt_string_view>);
    static_assert(!orderable_between<nt_string_view, const char *>);
}

TEST_CASE("equal views hash equally", "[nt_string_view]")
{
    const std::string a = "abc";
    const nt_string_view view = a;
    CHECK(std::hash<nt_string_view>{}(view) == std::hash<std::string_view>{}("abc"));
    CHECK(std::hash<nt_string_view>{}(nt_string_view()) == std::hash<std::string_view>{}(""));
}

TEST_CASE("substr and rm_prefix return new views", "[nt_string_view]")
{
    const std::string s = "abcdef";
    const nt_string_view view = s;

    const nt_string_view without = rm_prefix(view, 2);
    CHECK(without == std::string_view("cdef"));
    CHECK(without.c_str()[without.size()] == '\0');
    CHECK(view == std::string_view("abcdef")); // the original is unchanged

    const nt_string_view rest = substr(without, 3);
    CHECK(rest == std::string_view("f"));
    CHECK(rest.c_str() == s.c_str() + 5);
    CHECK(substr(view) == view);
    CHECK(substr(view, view.size()).empty());
    CHECK(substr(view, view.size()).c_str()[0] == '\0');
    CHECK(rm_prefix(view, 0) == substr(view, 0));

    static_assert(std::same_as<decltype(rm_prefix(view, 1)), nt_string_view>);
    static_assert(std::same_as<decltype(substr(view, 1)), nt_string_view>);

    if constexpr (contract_level >= 1)
    {
        test::scoped_throwing_contract_handler handler;
        CHECK_THROWS_AS(rm_prefix(view, 7), contract_failure);
        CHECK_THROWS_AS(substr(view, 7), contract_failure);
    }
}

TEST_CASE("swap", "[nt_string_view]")
{
    const std::string a = "a";
    const std::string b = "bb";
    nt_string_view va = a;
    nt_string_view vb = b;
    va.swap(vb);
    CHECK(va == b);
    CHECK(vb == a);
}

TEST_CASE("iterators", "[nt_string_view]")
{
    const std::string s = "abc";
    const nt_string_view view = s;
    CHECK(std::string(view.begin(), view.end()) == "abc");
    CHECK(std::string(view.rbegin(), view.rend()) == "cba");
    CHECK(view.end() - view.begin() == 3);
}

TEST_CASE("it is a view and a borrowed contiguous range", "[nt_string_view][concepts]")
{
    static_assert(std::ranges::contiguous_range<nt_string_view>);
    static_assert(std::ranges::sized_range<nt_string_view>);
    static_assert(std::ranges::view<nt_string_view>);
    static_assert(std::ranges::borrowed_range<nt_string_view>);
    static_assert(std::is_trivially_copyable_v<nt_string_view>);
    static_assert(std::constructible_from<std::string_view, nt_string_view>);
}

TEST_CASE("output and formatting", "[nt_string_view]")
{
    const std::string s("a\0b", 3); // characters before the terminator may be CharT()
    const nt_string_view view = s;
    std::ostringstream out;
    out << view;
    CHECK(out.str() == s);

    CHECK(std::format("[{}]", view) == std::string("[a\0b]", 5));
    CHECK(std::format("{}", view).size() == 3);
    CHECK(std::format("[{:>5}]", nt_string_view("xy", src_capacity(3))) == "[   xy]");
}
