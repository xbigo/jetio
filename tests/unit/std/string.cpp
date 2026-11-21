#include <jio/std/string.hpp>
#include <jio/std/string_io.hpp>

#include <atomic>
#include <cstring>
#include <format>
#include <memory_resource>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace jio;

namespace
{
    /// Counts what the strings take from it and gives back.
    class counting_resource : public std::pmr::memory_resource
    {
    public:
        int allocations = 0;
        int deallocations = 0;

    private:
        void *do_allocate(std::size_t bytes, std::size_t align) override
        {
            ++allocations;
            return std::pmr::new_delete_resource()->allocate(bytes, align);
        }
        void do_deallocate(void *p, std::size_t bytes, std::size_t align) override
        {
            ++deallocations;
            std::pmr::new_delete_resource()->deallocate(p, bytes, align);
        }
        bool do_is_equal(const std::pmr::memory_resource &other) const noexcept override
        {
            return this == &other;
        }
    };

    std::string text_of(std::size_t n)
    {
        std::string text(n, 'a');
        for (std::size_t i = 0; i < n; ++i)
            text[i] = static_cast<char>('a' + i % 26);
        return text;
    }

    constinit string global_empty;
    constinit string global_literal = "constant"_str;

    constexpr char pool_text[] = "from a pool";

    template <typename A, typename B>
    concept equality_comparable_between = requires(const A &a, const B &b) { a == b; };
    template <typename... Pieces>
    concept can_concat = requires(const Pieces &...pieces) { concat(pieces...); };
    template <typename A, typename B>
    concept can_add = requires(const A &a, const B &b) { a + b; };
    template <typename A, typename B>
    concept can_jio_add = requires(const A &a, const B &b) { jio::operator+(a, b); };
} // namespace

TEST_CASE("a string is string_object_size bytes and ends with a terminator", "[string]")
{
    static_assert(sizeof(string) == string_object_size && sizeof(wstring) == string_object_size &&
                  sizeof(u32string) == string_object_size);
    static_assert(nt_string_view_like<string>);
    static_assert(std::is_nothrow_move_constructible_v<string> &&
                  std::is_nothrow_copy_constructible_v<string>);
    static_assert(!std::is_convertible_v<const char *, string>); // only explicitly

    string s;
    CHECK(s.empty());
    CHECK(s.size() == 0);
    CHECK(s.c_str()[0] == '\0');
    CHECK(s.layout() == string_layout::tiny);
}

TEST_CASE("the layout follows the size", "[string]")
{
    counting_resource mr;
    const std::size_t tiny = string::tiny_capacity;
    int shared = 0;
    for (const std::size_t n : {std::size_t{0}, std::size_t{1}, tiny - 1, tiny, tiny + 1, tiny + 2,
                                std::size_t{100}, std::size_t{5000}})
    {
        const std::string text = text_of(n);
        const string s(std::string_view(text), mr);
        shared += n > tiny;
        CHECK(s.layout() == (n <= tiny ? string_layout::tiny : string_layout::shared));
        CHECK(s.size() == n);
        CHECK(std::string_view(s) == text);
        CHECK(s.data()[n] == '\0');
        CHECK(std::string_view(s.c_str()) == text);
    }
    CHECK(mr.allocations == mr.deallocations);
    CHECK(mr.allocations == shared);
}

TEST_CASE("a tiny string does not use the resource", "[string]")
{
    counting_resource mr;
    {
        const string a(std::string_view("short"), mr);
        const string b = a;
        CHECK(b == a);
    }
    CHECK(mr.allocations == 0);
}

TEST_CASE("characters may be the terminator", "[string]")
{
    const std::string_view with_nul("ab\0cd", 5);
    const string tiny(with_nul);
    CHECK(tiny.size() == 5);
    CHECK(std::string_view(tiny) == with_nul);

    std::string long_text(40, 'x');
    long_text[20] = '\0';
    const string shared{std::string_view(long_text)};
    CHECK(shared.size() == 40);
    CHECK(std::string_view(shared) == long_text);
    CHECK("a\0b"_str.size() == 3);
}

TEST_CASE("a copy of a shared string shares the block", "[string]")
{
    counting_resource mr;
    {
        const string a(std::string_view(text_of(40)), mr);
        const string b = a;
        string c;
        c = a;
        CHECK(a.data() == b.data());
        CHECK(a.data() == c.data());
        CHECK(mr.allocations == 1);
    }
    CHECK(mr.deallocations == 1);
}

TEST_CASE("a moved from string is empty", "[string]")
{
    for (const std::size_t n : {3u, 40u})
    {
        string a{std::string_view(text_of(n))};
        const string b = std::move(a);
        CHECK(a.empty()); // NOLINT(bugprone-use-after-move)
        CHECK(a.layout() == string_layout::tiny);
        CHECK(a.c_str()[0] == '\0');
        CHECK(b.size() == n);

        string c;
        c = std::move(const_cast<string &>(b));
        CHECK(c.size() == n);
    }
}

TEST_CASE("assignment replaces the value, whatever the layouts", "[string]")
{
    counting_resource mr;
    const std::string small = text_of(5);
    const std::string big = text_of(50);
    {
        string a(std::string_view(big), mr);
        const string b{std::string_view(small)};
        a = b; // the block is released
        CHECK(std::string_view(a) == small);
        CHECK(mr.deallocations == 1);

        a = "literal"_str;
        CHECK(a.layout() == string_layout::borrowed);
        a = string(std::string_view(big), mr);
        CHECK(std::string_view(a) == big);

        const string &self = a;
        a = self; // itself
        CHECK(std::string_view(a) == big);
        CHECK(std::string_view(a) == big);
    }
    CHECK(mr.allocations == mr.deallocations);
}

TEST_CASE("swap exchanges the values", "[string]")
{
    string a{std::string_view("tiny")};
    string b{std::string_view(text_of(30))};
    swap(a, b);
    CHECK(a.size() == 30);
    CHECK(b == "tiny"_str);
}

TEST_CASE("a borrowed string copies nothing", "[string]")
{
    const string s(borrow, nt_string_view(pool_text));
    CHECK(s.layout() == string_layout::borrowed);
    CHECK(s.data() == pool_text);
    CHECK(s.size() == 11);

    const string copy = s;
    CHECK(copy.data() == pool_text); // a copy borrows the same characters
    CHECK(copy.layout() == string_layout::borrowed);

    // the way out of the pool: a string made from the characters is independent
    const string own{std::string_view(s)};
    CHECK(own.data() != pool_text);
    CHECK(own.layout() == string_layout::tiny);
}

TEST_CASE("the literal operator", "[string]")
{
    const string s = "hello"_str;
    CHECK(s.layout() == string_layout::borrowed);
    CHECK(s == "hello"_nt);
    CHECK(std::string_view(s.c_str()) == "hello");

    CHECK(global_empty.empty());
    CHECK(global_literal == "constant"_str);
    CHECK(global_literal.layout() == string_layout::borrowed);
}

TEST_CASE("a string converts to the views and compares with them", "[string]")
{
    const string s{std::string_view("abc")};
    const nt_string_view view = s; // implicit
    CHECK(view.size() == 3);
    CHECK(view.c_str() == s.c_str());
    const auto size_of = [](nt_string_view v) { return v.size(); };
    CHECK(size_of(s) == 3);

    CHECK(s == string{std::string_view("abc")});
    CHECK(s == "abc"_nt);
    CHECK("abc"_nt == s);
    CHECK(s == std::string_view("abc"));
    CHECK(s == std::string("abc"));
    CHECK(s != "abd"_nt);
    CHECK(s < "abd"_nt);
    CHECK((s <=> "abc"_nt) == std::strong_ordering::equal);

    static_assert(!equality_comparable_between<string, char[4]>); // an array is not a string
}

TEST_CASE("equal strings of any layout hash alike, and like the views", "[string]")
{
    const std::string text = text_of(40);
    const string shared{std::string_view(text)};
    const string other{std::string_view(text)};
    const std::hash<string> hash;
    CHECK(hash(shared) == hash(other));
    CHECK(hash(shared) == std::hash<std::string_view>{}(text));
    CHECK(hash(shared) == std::hash<nd_string_view>{}(nd_string_view(shared)));
    CHECK(hash("hello"_str) == hash(string{std::string_view("hello")}));

    std::unordered_set<string> set;
    set.insert(shared);
    CHECK(set.contains(other));
    CHECK(!set.contains("hello"_str));
}

TEST_CASE("elements, iterators and the empty string", "[string]")
{
    const string s{std::string_view("hello")};
    CHECK(s[1] == 'e');
    CHECK(std::string(s.begin(), s.end()) == "hello");
    CHECK(std::string(s.rbegin(), s.rend()) == "olleh");
    CHECK(!s.empty());
}

TEST_CASE("concat makes the string of the pieces with one allocation", "[string]")
{
    counting_resource mr;
    // long enough not to be tiny, whatever string_object_size is
    const std::string long_name = text_of(100);
    const string dir{std::string_view(long_name)};
    const std::string file = "file";
    {
        const string path = concat_in(mr, dir, "/"_nt, file, ".txt"_nt);
        CHECK(path == long_name + "/file.txt");
        CHECK(path.layout() == string_layout::shared);
        CHECK(mr.allocations == 1);
    }
    CHECK(mr.deallocations == 1);

    CHECK(concat("a"_nt, "b"_nt).layout() == string_layout::tiny);
    CHECK(concat(dir) == dir);
    CHECK(concat(std::string_view(), std::string_view()).empty());
}

TEST_CASE("concat refuses what cannot be a string", "[string]")
{
    static_assert(!can_concat<char[2], char[2]>);
    static_assert(!can_concat<string, std::wstring_view>);

    // The sum is checked before anything is read.
    const std::string_view huge(pool_text, string::max_size() + 1);
    CHECK_THROWS_AS(concat(huge, "x"_nt), std::length_error);
    const std::string_view widest(pool_text, std::numeric_limits<std::ptrdiff_t>::max());
    CHECK_THROWS_AS(concat(widest, widest, widest), std::length_error); // the sum overflows
}

TEST_CASE("a sum of strings is a concator that converts once", "[string]")
{
    counting_resource mr;
    const std::string long_name = text_of(100);
    const string a{std::string_view(long_name)};
    const string b{std::string_view("/file")};

    string s = a + b + ".txt"_nt;
    CHECK(s == long_name + "/file.txt");

    const string t = a + b;
    CHECK(t == long_name + "/file");

    // another resource: the named conversion
    const string u = (a + b + "xyz"_nt).to_string(mr);
    CHECK(u == long_name + "/filexyz");
    CHECK(mr.allocations == 1);

    // an lvalue does not convert; it would outlive the pieces it refers to
    const auto kept = a + b;
    static_assert(std::is_convertible_v<decltype(a + b), string>);
    static_assert(!std::is_convertible_v<decltype(kept) &, string>);
    static_assert(!std::is_convertible_v<const decltype(kept) &, string>);
    CHECK(kept.size() == 105);
}

TEST_CASE("operator+ needs a string of ours", "[string]")
{
    static_assert(!can_jio_add<std::string, std::string>);
    static_assert(can_add<std::string, string>);
    static_assert(!can_add<string, char[4]>); // an array is not a string
    static_assert(!can_add<string, wstring>);
}

TEST_CASE("join puts the separator between the pieces", "[string]")
{
    counting_resource mr;
    const std::string piece = text_of(40);
    const std::vector<std::string> pieces{piece, piece, piece};
    CHECK(join(pieces, ", "_nt, mr) == piece + ", " + piece + ", " + piece);
    CHECK(mr.allocations == 1);
    CHECK(join(std::vector<string>{}, ","_nt).empty());
    CHECK(join(std::vector<string>{"one"_str}, ","_nt) == "one"_nt);
}

TEST_CASE("a string is written to streams and formatted", "[string]")
{
    const string s = "text"_str;
    std::ostringstream os;
    os << s;
    CHECK(os.str() == "text");
    CHECK(std::format("[{}] [{:>6}]", s, s) == "[text] [  text]");
}

TEST_CASE("other character types use their own tiny size", "[string]")
{
    constexpr std::size_t tiny = wstring::tiny_capacity;
    static_assert(tiny == string_object_size / sizeof(wchar_t) - 1);
    for (const std::size_t n : {std::size_t{0}, tiny, tiny + 1, std::size_t{20}})
    {
        const std::wstring text(n, L'w');
        const wstring s{std::wstring_view(text)};
        CHECK(s.layout() == (n <= tiny ? string_layout::tiny : string_layout::shared));
        CHECK(std::wstring_view(s) == text);
        CHECK(s.c_str()[n] == L'\0');
    }
}

TEST_CASE("copies of one shared string may be made and destroyed in several threads", "[string]")
{
    counting_resource mr;
    {
        const string shared(std::string_view(text_of(64)), mr);
        std::vector<std::thread> threads;
        std::atomic<int> good{0};
        for (int t = 0; t < 4; ++t)
            threads.emplace_back(
                [&]
                {
                    for (int i = 0; i < 2000; ++i)
                    {
                        const string copy = shared;
                        string other = copy;
                        if (other.size() == 64)
                            ++good;
                    }
                });
        for (auto &thread : threads)
            thread.join();
        CHECK(good == 8000);
    }
    CHECK(mr.allocations == 1);
    CHECK(mr.deallocations == 1);
}

TEST_CASE("a range of characters is a source of a string, a range of bytes is not", "[string]")
{
    const std::vector<char> chars{'a', 'b', 'c'};
    const std::span<const char> view(chars);
    CHECK(string(view) == "abc"_nt);
    CHECK(string(std::span<char>(const_cast<std::vector<char> &>(chars))) == "abc"_nt);
    CHECK(string(chars) == "abc"_nt);
    CHECK(concat(view, "-"_nt, chars) == "abc-abc"_nt);
    CHECK(join(std::vector<std::span<const char>>{view, view}, ","_nt) == "abc,abc"_nt);
    const string sum = string{} + view;
    CHECK(sum == "abc"_nt);

    static_assert(!std::is_constructible_v<string, std::span<const std::byte>>);
    static_assert(!can_concat<std::span<const std::byte>, string>);
}

// The layout is told in the tests by the bytes of the object, so a mistake in the placement of
// the fields (the tag in the last byte, the size in the 56 bits below it) shows on a platform
// whose pointer is not 64 bit as well.
TEST_CASE("the tag is the last byte and the size the bytes below it", "[string]")
{
    const auto bytes_of = [](const string &s)
    { return reinterpret_cast<const unsigned char *>(&s); };
    const auto word_of = [&](const string &s)
    {
        std::uint64_t word = 0;
        std::memcpy(&word, bytes_of(s) + string_object_size - 8, 8);
        return word;
    };
    const std::uint64_t size_mask = (std::uint64_t{1} << 56) - 1;

    const string tiny{std::string_view("abc")};
    CHECK(bytes_of(tiny)[string_object_size - 1] < 0x80);
    CHECK(string::tiny_capacity - bytes_of(tiny)[string_object_size - 1] == 3);

    const string shared{std::string_view(text_of(string::tiny_capacity + 5))};
    CHECK(bytes_of(shared)[string_object_size - 1] >= 0x80);
    CHECK((word_of(shared) & size_mask) == shared.size());

    const string borrowed(borrow, nt_string_view(pool_text));
    CHECK(bytes_of(borrowed)[string_object_size - 1] >= 0x80);
    CHECK(bytes_of(borrowed)[string_object_size - 1] != bytes_of(shared)[string_object_size - 1]);
    CHECK((word_of(borrowed) & size_mask) == borrowed.size());

    // the tag does not mix with the size or the pointer, whatever their widths
    CHECK(shared.size() == string::tiny_capacity + 5);
    CHECK(borrowed.size() == sizeof(pool_text) - 1);
    CHECK(borrowed.data() == pool_text);
    CHECK(shared.layout() != borrowed.layout());
}

TEST_CASE("the longest string is one that a std::ptrdiff_t can measure", "[string]")
{
    static_assert(string::max_size() <=
                  static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()));
    static_assert(string::max_size() >= string::tiny_capacity);
    // the block is that and a little more, which must not wrap
    const std::string_view too_long(pool_text, string::max_size() + 1);
    CHECK_THROWS_AS(string(too_long), std::length_error);
}

TEST_CASE("a C string is copied up to its terminator", "[string]")
{
    const char *text = "a string that is too long to be tiny, whatever string_object_size is";
    const string s(text);
    CHECK(s.layout() == string_layout::shared);
    CHECK(s == std::string_view(text));
    CHECK(s.data() != text);

    // an array is taken as a C string too: the characters up to the first terminator
    const char buffer[] = {'a', 'b', '\0', 'c', '\0'};
    CHECK(string(buffer) == "ab"_nt);

    counting_resource mr;
    const string in_pool(text, mr);
    CHECK(mr.allocations == 1);

    string t;
    t = text;
    CHECK(t == s);
    t = "tiny";
    CHECK(t == "tiny"_nt);
    t = {};
    CHECK(t.empty());
}
