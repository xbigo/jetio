#include <jetio/std/string_view.hpp>
#include <jetio/std/string_view_io.hpp>

#include <array>
#include <format>
#include <functional>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace jetio;

namespace
{
    constexpr int which(const nd_string_view_like auto &)
    {
        return 1;
    }
    constexpr int which(const ns_string_view_like auto &)
    {
        return 2;
    }
    constexpr int which(const nt_string_view_like auto &)
    {
        return 3;
    }
} // namespace

TEST_CASE("the views convert towards the weaker guarantee", "[string_view][concepts]")
{
    static_assert(std::convertible_to<nt_string_view, ns_string_view>);
    static_assert(std::convertible_to<nt_string_view, nd_string_view>);
    static_assert(std::convertible_to<ns_string_view, nd_string_view>);
    static_assert(!std::convertible_to<nd_string_view, ns_string_view>);
    static_assert(!std::convertible_to<ns_string_view, nt_string_view>);
    static_assert(!std::convertible_to<nd_string_view, nt_string_view>);
    static_assert(!std::convertible_to<std::string_view, nd_string_view>);
    static_assert(!std::convertible_to<const char *, nd_string_view>);
    static_assert(std::convertible_to<std::string, nd_string_view>);
    static_assert(std::convertible_to<std::string, ns_string_view>);
    static_assert(std::convertible_to<nd_string_view, std::string_view>);

    // overloads pick the most specific concept
    static_assert(which(nd_string_view()) == 1);
    static_assert(which(ns_string_view()) == 2);
    static_assert(which(nt_string_view()) == 3);
    static_assert(which(std::string()) == 3);
}

TEST_CASE("the views are views over the same layout", "[string_view][concepts]")
{
    static_assert(sizeof(nd_string_view) == sizeof(std::string_view));
    static_assert(sizeof(ns_string_view) == sizeof(std::string_view));
    static_assert(sizeof(nt_string_view) == sizeof(std::string_view));
    static_assert(std::is_trivially_copyable_v<nd_string_view>);
    static_assert(std::is_trivially_copyable_v<ns_string_view>);
    static_assert(std::ranges::contiguous_range<nd_string_view>);
    static_assert(std::ranges::view<nd_string_view> && std::ranges::borrowed_range<nd_string_view>);
    static_assert(std::ranges::view<ns_string_view> && std::ranges::borrowed_range<ns_string_view>);
    static_assert(std::ranges::view<nt_string_view> && std::ranges::borrowed_range<nt_string_view>);
}

TEST_CASE("a default view is empty and its end is a terminator", "[string_view]")
{
    constexpr nd_string_view nd;
    constexpr ns_string_view ns;
    static_assert(nd.empty() && ns.empty());
    CHECK(nd.is_terminated());
    CHECK(ns.is_terminated());
    CHECK(nd == std::string_view());
    CHECK(as_nt(ns).has_value());
}

TEST_CASE("a stronger view converts without changing the characters", "[string_view]")
{
    const std::string s = "abc";
    const nt_string_view nt = s;
    const ns_string_view ns = nt;
    const nd_string_view nd = ns;
    CHECK(ns == std::string_view("abc"));
    CHECK(nd == std::string_view("abc"));
    CHECK(nd.data() == s.data());
    CHECK(nd.is_terminated());

    const ns_string_view from_string = s;
    const nd_string_view from_string_nd = s;
    CHECK(from_string == nt);
    CHECK(from_string_nd == nt);
}

TEST_CASE("shortening a view keeps the guarantee that survives", "[string_view]")
{
    const std::string s = "abcdef";
    const nt_string_view nt = s;

    const auto head = substr(nt, 0, 3);
    static_assert(std::same_as<std::remove_const_t<decltype(head)>, ns_string_view>);
    CHECK(head == std::string_view("abc"));
    CHECK(!head.is_terminated()); // the 'd' follows
    CHECK(!as_nt(head).has_value());

    // a shortened view that happens to reach the end can be looked at again
    const auto whole = substr(nt, 0, nt.size());
    CHECK(whole.is_terminated());
    const auto terminated = as_nt(whole);
    if (terminated.has_value())
        CHECK(terminated->c_str() == s.c_str());
    else
        FAIL("a view that reaches the terminator is terminated");

    const auto no_tail = rm_suffix(nt, 2);
    static_assert(std::same_as<std::remove_const_t<decltype(no_tail)>, ns_string_view>);
    CHECK(no_tail == std::string_view("abcd"));

    const auto tail = rm_prefix(nt, 2);
    static_assert(std::same_as<std::remove_const_t<decltype(tail)>, nt_string_view>);
    CHECK(tail == std::string_view("cdef"));

    // the weaker views keep their own level, whatever is cut
    const auto ns_part = substr(head, 1, 1);
    static_assert(std::same_as<std::remove_const_t<decltype(ns_part)>, ns_string_view>);
    CHECK(ns_part == std::string_view("b"));
    const nd_string_view nd = head;
    const auto nd_part = rm_suffix(rm_prefix(nd, 1), 1);
    static_assert(std::same_as<std::remove_const_t<decltype(nd_part)>, nd_string_view>);
    CHECK(nd_part == std::string_view("b"));

    // the original is not changed
    CHECK(nt == std::string_view("abcdef"));
}

TEST_CASE("a view built on the caller's word", "[string_view]")
{
    const std::string s = "abcdef";
    const std::string_view whole = s;
    const std::string_view head = whole.substr(0, 3);

    const nd_string_view nd(assume, head);
    const ns_string_view ns(assume, head);
    CHECK(nd == head);
    CHECK(ns == head);
    CHECK(!nd.is_terminated());
    CHECK(nt_string_view(assume, whole) == whole);
}

TEST_CASE("the views share one interface", "[string_view]")
{
    const std::string s = "abc";
    const nt_string_view nt = s;
    ns_string_view ns = nt;
    nd_string_view nd = nt;

    CHECK(std::string(ns.begin(), ns.end()) == "abc");
    CHECK(std::string(nd.rbegin(), nd.rend()) == "cba");
    CHECK(nd.front() == 'a');
    CHECK(nd.back() == 'c');
    CHECK(nd.is_terminated());

    // comparison across the levels looks at the characters
    CHECK(nt == ns);
    CHECK(ns == nd);
    CHECK(nd == std::string_view("abc"));
    CHECK(nd == s);
    CHECK(nd < std::string_view("abd"));
    CHECK(std::hash<nd_string_view>{}(nd) == std::hash<std::string_view>{}("abc"));
    CHECK(std::hash<ns_string_view>{}(ns) == std::hash<nt_string_view>{}(nt));

    // swap replaces the whole value
    nd_string_view other = substr(nd, 1);
    nd.swap(other);
    CHECK(nd == std::string_view("bc"));
    CHECK(other == std::string_view("abc"));
    ns.swap(ns);
    CHECK(ns == nt);
}

TEST_CASE("the views are written and formatted as their characters", "[string_view]")
{
    const std::string s("a\0b", 3);
    const ns_string_view ns = s;
    const nd_string_view nd = s;
    std::ostringstream out;
    out << ns << '|' << nd;
    CHECK(out.str() == std::string("a\0b|a\0b", 7));
    CHECK(std::format("[{}]", ns) == std::string("[a\0b]", 5));
    CHECK(std::format("[{:>5}]", nd_string_view(assume, std::string_view("xy"))) == "[   xy]");
}

TEMPLATE_TEST_CASE("a pointer with its capacity is scanned for the terminator", "[string_view]",
                   nd_string_view, ns_string_view, nt_string_view)
{
    static constexpr char text[] = "abc\0def";
    const TestType view(text, src_capacity(sizeof(text)));
    CHECK(view == std::string_view("abc"));
    CHECK(view.data() == text);
    CHECK(view.is_terminated());
}

TEMPLATE_TEST_CASE("a length and a capacity give a view of a buffer", "[string_view]",
                   nd_string_view, ns_string_view, nt_string_view)
{
    static constexpr char buffer[] = {'a', 'b', '\0', 'c'};
    const TestType view(buffer, 2, src_capacity(4));
    CHECK(view == std::string_view("ab"));
    CHECK(view.data() == buffer);
    CHECK(view.is_terminated());
}

TEST_CASE("what each view asks of the buffer after the end", "[string_view]")
{
    static constexpr char open[] = {'a', 'b', 'c'};
    static constexpr char closed[] = {'a', 'b', 'c', '\0'};

    // nd: the end only has to be readable
    const nd_string_view nd(open, 2, src_capacity(3));
    CHECK(nd == std::string_view("ab"));
    CHECK(!nd.is_terminated());

    // ns: a terminator follows, further on than the end
    const ns_string_view ns(closed, 2, src_capacity(4));
    CHECK(ns == std::string_view("ab"));
    CHECK(!ns.is_terminated());

    // nt: the end is the terminator
    const nt_string_view nt(closed, 3, src_capacity(4));
    CHECK(nt == std::string_view("abc"));
    CHECK(nt.is_terminated());
}

TEST_CASE("the views work in constant expressions", "[string_view]")
{
    static constexpr char text[] = "hello";
    constexpr nt_string_view z(text, src_capacity(sizeof(text)));
    constexpr auto head = substr(z, 0, 2);
    constexpr auto tail = rm_prefix(z, 2);
    constexpr nd_string_view nd = head;
    constexpr ns_string_view ns(text, 3, src_capacity(sizeof(text)));

    static_assert(z.size() == 5 && z.c_str()[5] == '\0' && z.front() == 'h' && z.back() == 'o');
    static_assert(head.size() == 2 && !head.is_terminated() && nd == head);
    static_assert(tail.size() == 3 && tail.c_str() == text + 2 && as_nt(tail).has_value());
    static_assert(ns.size() == 3);
    CHECK(head == std::string_view("he"));
}
