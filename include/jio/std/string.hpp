#pragma once

// An owning, immutable string, cheap to copy. See docs/design/string.md.
//
// A string is string_object_size bytes (16, on every platform) and has one of three layouts,
// which only its destructor, copy and move constructors, and data() and size() tell apart:
//   tiny      up to 15 characters (for char) in the object, no allocation
//   shared    in a block with a reference count, which a copy shares
//   borrowed  characters that someone else keeps alive: a literal, or what a pool owns; nothing
//             is counted or freed
// A string always ends with a terminator, so c_str() is data(), and it converts to an
// nt_string_view without a check that can fail. The characters may contain CharT() too.
//
// The strings of the project are UTF-8 in char; the template supports the other character types
// like the string views do.
//
// Making a string: from anything that converts to a string view but a pointer or an array (a
// copy, explicit); `"text"_str`, or the constructor with the tag borrow (borrowed, no copy); by
// concatenation (concat, join, operator+ whose result is only a concator, see there). The
// storage of a shared string comes from a std::pmr::memory_resource, which must outlive every
// string that uses it. A function that makes a string takes it as the last parameter, the
// default being the default resource of std::pmr; concat_in takes it first.
//
// No member changes a string: assignment and swap replace the whole value. Copies of a shared
// string may be made and destroyed from several threads.

#include <jio/std/contract.hpp>
#include <jio/std/string_view.hpp>
#include <jio/std/string_view_concepts.hpp>

#include <algorithm>
#include <atomic>
#include <bit>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <memory_resource>
#include <new>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

namespace jio
{
    /// Marks the construction of a borrowed string: on the caller's word that the characters
    /// outlive the string and every copy of it.
    struct borrow_t
    {
        explicit borrow_t() = default;
    };
    inline constexpr borrow_t borrow{};

    /// The size of a string object in bytes, the same on every platform. At least 16 and a
    /// multiple of 8; a larger value makes longer strings tiny (24 gives 23 characters, 32 gives
    /// 31), at the price of a larger object. The layout adapts; nothing else depends on it.
    inline constexpr std::size_t string_object_size = 16;
    static_assert(string_object_size >= 16 && string_object_size % 8 == 0);

    enum class string_layout
    {
        tiny,
        shared,
        borrowed
    };

    template <typename CharT, typename Traits = std::char_traits<CharT>> class basic_string;
    template <typename L, typename R> class concator;

    namespace detail
    {
        struct string_access;
        struct concat_access
        {
            /// Writes the pieces of the concator to `out`, which has room for its size.
            template <typename L, typename R, typename CharT>
            static CharT *copy(const concator<L, R> &c, CharT *out) noexcept;
        };

        /// Bytes in front of the pointer of a string that is larger than 16 bytes.
        template <std::size_t K> struct padding
        {
            std::byte bytes[K]{};
        };
        template <> struct padding<0>
        {
        };

        template <typename T> struct is_basic_string : std::false_type
        {
        };
        template <typename C, typename T>
        struct is_basic_string<basic_string<C, T>> : std::true_type
        {
        };
        template <typename T> struct is_concator : std::false_type
        {
        };
        template <typename L, typename R> struct is_concator<concator<L, R>> : std::true_type
        {
        };

        /// What operator+ concatenates: anything that converts to a string view, but a pointer or
        /// an array. Only the characters are read, so the end of a piece need not be known.
        template <typename T>
        concept concat_operand = string_view_source<T> || is_concator<T>::value;

        /// What gives operator+ the right to exist: one operand of ours.
        template <typename T>
        concept ours = string_view_family<T> || is_basic_string<T>::value || is_concator<T>::value;

        /// @throws std::length_error if the sum does not fit in std::size_t
        template <typename... Sizes> std::size_t checked_sum(Sizes... sizes)
        {
            std::size_t total = 0;
            for (const std::size_t n : {static_cast<std::size_t>(sizes)...})
            {
                if (n > std::numeric_limits<std::size_t>::max() - total)
                    throw std::length_error("jio::string: too long");
                total += n;
            }
            return total;
        }

        inline std::pmr::memory_resource &default_resource() noexcept
        {
            return *std::pmr::get_default_resource();
        }
    } // namespace detail

    template <typename CharT, typename Traits> class basic_string
    {
        static_assert(character<CharT>);
        static_assert(std::endian::native == std::endian::little,
                      "the layout puts the tag in the last byte");

    public:
        using traits_type = Traits;
        using value_type = CharT;
        using pointer = const CharT *;
        using const_pointer = const CharT *;
        using reference = const CharT &;
        using const_reference = const CharT &;
        using const_iterator = const CharT *;
        using iterator = const_iterator;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;
        using reverse_iterator = const_reverse_iterator;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;
        using string_view_type = std::basic_string_view<CharT, Traits>;
        using nt_string_view_type = basic_nt_string_view<CharT, Traits>;

        /// The most characters a tiny string holds: the last element of the object keeps the
        /// room left, which is the terminator when there is none.
        static constexpr size_type tiny_capacity = string_object_size / sizeof(CharT) - 1;

        /// The size is kept in 56 bits, beside the tag; and the block, whose size must be a
        /// std::ptrdiff_t, must fit in memory.
        static constexpr size_type max_size() noexcept
        {
            constexpr std::uint64_t fits = (std::uint64_t{1} << 56) - 1;
            constexpr std::uint64_t memory =
                (static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max()) -
                 sizeof(block)) /
                    sizeof(CharT) -
                1;
            return static_cast<size_type>(std::min(fits, memory));
        }

        // --- constructors -----------------------------------------------------------------

        /// An empty string.
        constexpr basic_string() noexcept { reset_(); }

        /// A borrowed string: no copy, nothing is counted or freed.
        /// @pre the characters of `text` (and its terminator) are alive for as long as this string
        ///      and any copy of it; the caller promises it
        /// @pre text.size() <= max_size() (critical)
        constexpr basic_string(borrow_t, nt_string_view_type text) noexcept
            : m_(heap_rep{text.data(), pack_(text.size(), tag_borrowed)})
        {
            JIO_Expects_critical(text.size() <= max_size());
        }

        /// A copy of the characters of `text`, tiny if they are few, else in a block from `mr`.
        /// @throws std::length_error if text.size() > max_size(), std::bad_alloc
        template <typename U>
            requires(string_view_source<U> && std::same_as<char_type_t<U>, CharT> &&
                     std::constructible_from<string_view_type, const U &> &&
                     !std::same_as<std::remove_cvref_t<U>, basic_string>)
        explicit basic_string(const U &text,
                              std::pmr::memory_resource &mr = detail::default_resource())
            : basic_string(copy_of_(string_view_type(text), mr))
        {
        }

        /// The characters of a concatenation, see concator.
        /// @throws std::length_error, std::bad_alloc
        template <typename L, typename R>
            requires std::same_as<typename concator<L, R>::value_type, CharT>
        explicit basic_string(const concator<L, R> &text,
                              std::pmr::memory_resource &mr = detail::default_resource())
            : basic_string(build_(mr, text.size(), [&](CharT *out) noexcept
                                  { return detail::concat_access::copy(text, out); }))
        {
        }

        /// Shares the block of a shared string.
        basic_string(const basic_string &other) noexcept : m_(other.m_)
        {
            if (layout() == string_layout::shared)
                block_()->count.fetch_add(1, std::memory_order_relaxed);
        }

        /// Takes the value; `other` is an empty string afterwards.
        basic_string(basic_string &&other) noexcept : m_(other.m_) { other.reset_(); }

        basic_string &operator=(const basic_string &other) noexcept
        {
            basic_string copy(other);
            swap(copy);
            return *this;
        }

        basic_string &operator=(basic_string &&other) noexcept
        {
            basic_string moved(std::move(other));
            swap(moved);
            return *this;
        }

        constexpr ~basic_string()
        {
            if consteval
            {
                return;
            }
            else
            {
                if (layout() == string_layout::shared)
                    release_();
            }
        }

        /// Replaces the whole value of both strings.
        void swap(basic_string &other) noexcept { std::swap(m_, other.m_); }
        friend void swap(basic_string &a, basic_string &b) noexcept { a.swap(b); }

        // --- observers --------------------------------------------------------------------

        string_layout layout() const noexcept
        {
            const unsigned char tag = tag_();
            if (tag < tag_shared)
                return string_layout::tiny;
            return tag == tag_shared ? string_layout::shared : string_layout::borrowed;
        }

        const_pointer data() const noexcept
        {
            return layout() == string_layout::tiny ? m_.tiny.chars : m_.heap.data;
        }
        size_type size() const noexcept
        {
            if (layout() == string_layout::tiny)
                return tiny_capacity - static_cast<size_type>(m_.tiny.chars[tiny_capacity]);
            return static_cast<size_type>(m_.heap.size_and_tag & size_mask);
        }

        /// The characters, which are followed by the terminator.
        const_pointer c_str() const noexcept { return data(); }

        const_iterator begin() const noexcept { return data(); }
        const_iterator end() const noexcept { return data() + size(); }
        const_iterator cbegin() const noexcept { return begin(); }
        const_iterator cend() const noexcept { return end(); }
        const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
        const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }

        bool empty() const noexcept { return size() == 0; }

        /// @pre pos < size() (critical)
        const_reference operator[](size_type pos) const noexcept
        {
            JIO_Expects_critical(pos < size());
            return data()[pos];
        }

        operator string_view_type() const noexcept { return {data(), size()}; }

        /// The terminator is known, so there is nothing to check but the promise itself.
        operator nt_string_view_type() const noexcept
        {
            return nt_string_view_type(assume, string_view_type(data(), size()));
        }

        // Comparison looks at the characters only. The other operand is any type that converts to
        // a string view, except a pointer or an array.
        template <detail::string_view_argument<string_view_type> U>
        friend bool operator==(const basic_string &lhs, const U &rhs) noexcept(
            std::is_nothrow_convertible_v<const U &, string_view_type>)
        {
            return string_view_type(lhs) == string_view_type(rhs);
        }

        template <detail::string_view_argument<string_view_type> U>
        friend auto operator<=>(const basic_string &lhs, const U &rhs) noexcept(
            std::is_nothrow_convertible_v<const U &, string_view_type>)
        {
            return string_view_type(lhs) <=> string_view_type(rhs);
        }

    private:
        friend struct detail::string_access;

        static constexpr unsigned char tag_shared = 0x80;
        static constexpr unsigned char tag_borrowed = 0x81;
        static constexpr std::uint64_t size_mask = (std::uint64_t{1} << 56) - 1;

        // In front of the characters of a shared string.
        struct block
        {
            std::atomic<size_type> count{1};
            std::pmr::memory_resource *resource;
            explicit block(std::pmr::memory_resource *r) noexcept : resource(r) {}
        };
        static_assert(sizeof(block) % alignof(CharT) == 0);

        struct tiny_rep
        {
            CharT chars[tiny_capacity + 1]{};
        };
        // The tag is the top byte of size_and_tag, which is the last byte of the object; the
        // padding in front of the pointer makes the object as large as the tiny one.
        struct heap_rep : detail::padding<string_object_size - 16>
        {
            const CharT *data;
            alignas(8) std::uint64_t size_and_tag;
            constexpr heap_rep(const CharT *d, std::uint64_t s) noexcept : data(d), size_and_tag(s)
            {
            }
        };
        union storage
        {
            tiny_rep tiny;
            heap_rep heap;
            constexpr storage() noexcept : tiny() {}
            constexpr explicit storage(heap_rep h) noexcept : heap(h) {}
        };
        static_assert(sizeof(storage) == string_object_size);

        static constexpr std::uint64_t pack_(size_type n, unsigned char tag) noexcept
        {
            return static_cast<std::uint64_t>(n) | (static_cast<std::uint64_t>(tag) << 56);
        }

        // The last byte of the object: the room left of a tiny string (below tag_shared), else
        // the tag. Read as bytes, since which member of the union is alive depends on it.
        unsigned char tag_() const noexcept
        {
            return reinterpret_cast<const unsigned char *>(&m_)[string_object_size - 1];
        }

        constexpr void reset_() noexcept
        {
            m_.tiny = tiny_rep{};
            m_.tiny.chars[tiny_capacity] = static_cast<CharT>(tiny_capacity);
        }

        block *block_() const noexcept
        {
            // The block is right in front of the characters (see build_). The launder hides where
            // the pointer comes from, else GCC warns of a write before a literal on a path that
            // a borrowed string never takes.
            auto *first =
                const_cast<unsigned char *>(reinterpret_cast<const unsigned char *>(m_.heap.data));
            return std::launder(reinterpret_cast<block *>(first - sizeof(block)));
        }

        void release_() noexcept
        {
            block *b = block_();
            if (b->count.fetch_sub(1, std::memory_order_acq_rel) == 1)
            {
                std::pmr::memory_resource *mr = b->resource;
                const std::size_t bytes = sizeof(block) + (size() + 1) * sizeof(CharT);
                b->~block();
                mr->deallocate(b, bytes, alignof(block));
            }
        }

        /// The one place that decides the layout from the size: n characters that `fill` writes
        /// to the pointer it is given, returning the end of what it wrote.
        /// @pre fill writes exactly n characters and does not throw
        /// @throws std::length_error if n > max_size(), std::bad_alloc
        template <typename Fill>
        static basic_string build_(std::pmr::memory_resource &mr, size_type n, Fill &&fill)
        {
            static_assert(std::is_nothrow_invocable_r_v<CharT *, Fill, CharT *>);
            if (n > max_size())
                throw std::length_error("jio::string: too long");

            basic_string s;
            if (n <= tiny_capacity)
            {
                CharT *out = s.m_.tiny.chars;
                [[maybe_unused]] CharT *end = fill(out);
                JIO_Ensures(end == out + n);
                out[n] = CharT();
                out[tiny_capacity] = static_cast<CharT>(tiny_capacity - n);
                return s;
            }
            void *raw = mr.allocate(sizeof(block) + (n + 1) * sizeof(CharT), alignof(block));
            ::new (raw) block(&mr);
            auto *out =
                reinterpret_cast<CharT *>(static_cast<unsigned char *>(raw) + sizeof(block));
            [[maybe_unused]] CharT *end = fill(out);
            JIO_Ensures(end == out + n);
            out[n] = CharT();
            s.m_.heap = heap_rep{out, pack_(n, tag_shared)};
            return s;
        }

        static basic_string copy_of_(string_view_type text, std::pmr::memory_resource &mr)
        {
            return build_(mr, text.size(), [&](CharT *out) noexcept
                          { return std::copy_n(text.data(), text.size(), out); });
        }

        storage m_{};
    };

    using string = basic_string<char>;
    using wstring = basic_string<wchar_t>;
    using u8string = basic_string<char8_t>;
    using u16string = basic_string<char16_t>;
    using u32string = basic_string<char32_t>;

    namespace detail
    {
        /// Lets the functions that make strings from pieces use the private build_.
        struct string_access
        {
            template <typename CharT, typename Traits, typename Fill>
            static basic_string<CharT, Traits> build(std::pmr::memory_resource &mr, std::size_t n,
                                                     Fill &&fill)
            {
                return basic_string<CharT, Traits>::build_(mr, n, std::forward<Fill>(fill));
            }
        };

        template <typename T> using operand_char_t = char_type_t<T>;

        template <typename T, typename CharT>
        CharT *copy_operand(const T &part, CharT *out) noexcept;
    } // namespace detail

    // --- concatenation --------------------------------------------------------------------

    /// What `a + b` is: the pieces and their total size, not a string. It converts to a string
    /// only as an rvalue, so `string s = a + b + c;` allocates once and copies each piece once,
    /// and `auto x = a + b; string s = x;` does not compile.
    /// A concator refers to its operands: it must not outlive the full expression that made it,
    /// so never keep one in a variable. (The explicit conversion, with another resource, takes
    /// a concator as well.)
    template <typename L, typename R> class concator
    {
    public:
        using value_type = detail::operand_char_t<L>;

        /// @return the size of the string it converts to
        std::size_t size() const noexcept { return m_size; }

        operator basic_string<value_type>() const && { return basic_string<value_type>(*this); }

    private:
        friend struct detail::concat_access;
        template <typename L2, typename R2>
            requires(detail::concat_operand<L2> && detail::concat_operand<R2> &&
                     (detail::ours<L2> || detail::ours<R2>) &&
                     std::same_as<detail::operand_char_t<L2>, detail::operand_char_t<R2>>)
        friend concator<L2, R2> operator+(const L2 &, const R2 &);

        concator(const L &left, const R &right)
            : m_left(left), m_right(right),
              m_size(detail::checked_sum(size_of_(left), size_of_(right)))
        {
        }

        template <typename T> static std::size_t size_of_(const T &part) noexcept
        {
            if constexpr (detail::is_concator<T>::value)
                return part.size();
            else
                return std::basic_string_view<value_type>(part).size();
        }

        const L &m_left;
        const R &m_right;
        std::size_t m_size;
    };

    namespace detail
    {
        template <typename L, typename R, typename CharT>
        CharT *concat_access::copy(const concator<L, R> &c, CharT *out) noexcept
        {
            return copy_operand(c.m_right, copy_operand(c.m_left, out));
        }

        template <typename T, typename CharT>
        CharT *copy_operand(const T &part, CharT *out) noexcept
        {
            if constexpr (is_concator<T>::value)
            {
                return concat_access::copy(part, out);
            }
            else
            {
                const std::basic_string_view<CharT> text(part);
                return std::copy_n(text.data(), text.size(), out);
            }
        }
    } // namespace detail

    /// The pieces of the concatenation, to be converted at once. At least one operand is a
    /// string, a string view of ours or a concator; all have the same character type.
    template <typename L, typename R>
        requires(detail::concat_operand<L> && detail::concat_operand<R> &&
                 (detail::ours<L> || detail::ours<R>) &&
                 std::same_as<detail::operand_char_t<L>, detail::operand_char_t<R>>)
    [[nodiscard]] concator<L, R> operator+(const L &left, const R &right)
    {
        return concator<L, R>(left, right);
    }

    /// The string of all the pieces in order, made with one allocation if it is not tiny.
    /// @throws std::length_error if it would be longer than max_size(), std::bad_alloc
    template <string_view_source First, string_view_source... Rest>
        requires(std::same_as<char_type_t<First>, char_type_t<Rest>> && ...)
    [[nodiscard]] basic_string<char_type_t<First>>
    concat_in(std::pmr::memory_resource &mr, const First &first, const Rest &...rest)
    {
        using char_type = char_type_t<First>;
        const std::basic_string_view<char_type> views[] = {
            std::basic_string_view<char_type>(first), std::basic_string_view<char_type>(rest)...};
        std::size_t n = 0;
        for (const auto &view : views)
            n = detail::checked_sum(n, view.size());
        return detail::string_access::build<char_type, std::char_traits<char_type>>(
            mr, n,
            [&](char_type *out) noexcept
            {
                for (const auto &view : views)
                    out = std::copy_n(view.data(), view.size(), out);
                return out;
            });
    }

    /// concat_in with the default resource of std::pmr.
    template <string_view_source First, string_view_source... Rest>
        requires(std::same_as<char_type_t<First>, char_type_t<Rest>> && ...)
    [[nodiscard]] basic_string<char_type_t<First>> concat(const First &first, const Rest &...rest)
    {
        return concat_in(detail::default_resource(), first, rest...);
    }

    /// The pieces of a range with `separator` between them, made with one allocation if the
    /// result is not tiny. The range is traversed twice.
    /// @throws std::length_error if it would be longer than max_size(), std::bad_alloc
    template <std::ranges::forward_range Range, string_view_source Separator>
        requires(
            string_view_source<std::ranges::range_value_t<Range>> &&
            std::same_as<char_type_t<std::ranges::range_value_t<Range>>, char_type_t<Separator>>)
    [[nodiscard]] basic_string<char_type_t<Separator>>
    join(const Range &pieces, const Separator &separator,
         std::pmr::memory_resource &mr = detail::default_resource())
    {
        using char_type = char_type_t<Separator>;
        const std::basic_string_view<char_type> glue(separator);
        std::size_t n = 0;
        bool first = true;
        for (const auto &piece : pieces)
        {
            n = detail::checked_sum(n, first ? 0 : glue.size(),
                                    std::basic_string_view<char_type>(piece).size());
            first = false;
        }
        return detail::string_access::build<char_type, std::char_traits<char_type>>(
            mr, n,
            [&](char_type *out) noexcept
            {
                bool is_first = true;
                for (const auto &piece : pieces)
                {
                    if (!is_first)
                        out = std::copy_n(glue.data(), glue.size(), out);
                    is_first = false;
                    const std::basic_string_view<char_type> view(piece);
                    out = std::copy_n(view.data(), view.size(), out);
                }
                return out;
            });
    }

    inline namespace literals
    {
        /// `"text"_str`: a borrowed string of the literal, which may contain `\0`.
        [[nodiscard]] consteval string operator""_str(const char *text, std::size_t length) noexcept
        {
            return string(borrow, nt_string_view(text, length, src_capacity(length + 1)));
        }
    } // namespace literals
} // namespace jio

// Equal to the hash of the std::basic_string_view with the same characters, so equal strings
// of any layout and the string views hash alike.
template <typename CharT, typename Traits> struct std::hash<jio::basic_string<CharT, Traits>>
{
    std::size_t operator()(const jio::basic_string<CharT, Traits> &text) const noexcept
    {
        return std::hash<std::basic_string_view<CharT, Traits>>{}(text);
    }
};
