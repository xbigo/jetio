#pragma once

// The string views with a guarantee about their end, from the weakest to the strongest:
//   nd_string_view  determinable: the end can be read, so is_terminated() can be asked
//   ns_string_view  sentinel: a terminator follows the end, somewhere in valid memory
//   nt_string_view  terminated: data()[size()] == CharT(), the terminator is not in the view
// The characters of a view may contain CharT() too. The concept string_view_family is any of them.
//
// Although the implementation supports several character types, this project uses only char,
// UTF-8 included: strings inside the project are UTF-8 in char, and another encoding is
// converted only at the boundary of an integration. The shortcuts for the other character types
// at the end are there for whoever needs them.
//
// Design: see docs/design/string-views.md, including how it differs from std::string_view.
//  - Immutable: no member changes a view in place. Operations are free functions returning new
//    views; only assignment and swap replace the whole value.
//  - Strict in: the guarantee is established when the view is built, never checked again.
//    Nothing that reaches a view as a pointer is trusted without an explicit source capacity,
//    and there is no implicit conversion from a pointer or an array.
//  - Primitives, which need the private members, have a meaning no other member covers, or
//    are needed by the standard algorithms: the constructors, data(), size(), c_str() of nt,
//    and the iterator functions and swap() of the base. The extensions (empty, operator[],
//    front, back, the conversion, the comparison) are composed from them.
//  - The views are independent types. What they share is the CRTP base string_view_ops, which
//    holds the pointer and the size; its constructors and assignment are private, for the
//    derived view only, so a stronger view cannot be weakened through a reference to its base.
//    A derived view has the constructors, which decide what is promised about the end, and
//    shortened_type: the view that keeps its guarantee when the end moves back.
//  - There is no searching or matching; use std::string_view(view) or an external algorithm.
//  - Input and output (<<, std::format) are in string_view_io.hpp, so that this header does
//    not depend on the streams.
//
// How to get a view: from a stronger view or a type that states such an end (std::string),
// both implicit; by shortening a view (rm_suffix, substr(pos, n)); with the tag assume from a
// std::basic_string_view, on the caller's word; or from a pointer with an explicit capacity:
// scanning for the terminator, or with the length n when the buffer is known.

#include <jetio/std/contract.hpp>
#include <jetio/std/string_view_concepts.hpp>

#include <compare>
#include <concepts>
#include <cstddef>
#include <functional>
#include <iterator>
#include <optional>
#include <ranges>
#include <string_view>
#include <type_traits>
#include <utility>

namespace jetio
{
    namespace detail
    {
        template <typename Derived, typename CharT, typename Traits> class string_view_ops
        {
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

            // Marks the views: the concept string_view_family looks for it.
            using string_view_family_tag = void;

            // --- primitives -------------------------------------------------------------------
            constexpr const_pointer data() const noexcept { return m_data; }
            constexpr size_type size() const noexcept { return m_size; }

            constexpr const_iterator begin() const noexcept { return m_data; }
            constexpr const_iterator end() const noexcept { return m_data + m_size; }
            constexpr const_iterator cbegin() const noexcept { return begin(); }
            constexpr const_iterator cend() const noexcept { return end(); }
            constexpr const_reverse_iterator rbegin() const noexcept
            {
                return const_reverse_iterator(end());
            }
            constexpr const_reverse_iterator rend() const noexcept
            {
                return const_reverse_iterator(begin());
            }
            constexpr const_reverse_iterator crbegin() const noexcept { return rbegin(); }
            constexpr const_reverse_iterator crend() const noexcept { return rend(); }

            /// Replaces the whole value of both views.
            constexpr void swap(Derived &other) noexcept
            {
                std::swap(m_data, other.m_data);
                std::swap(m_size, other.m_size);
            }

            // --- extensions -------------------------------------------------------------------
            constexpr bool empty() const noexcept { return m_size == 0; }

            /// pos == size() is allowed: the end of every view of ours can be read.
            /// @pre pos <= size() (critical)
            constexpr const_reference operator[](size_type pos) const noexcept
            {
                JIO_Expects_critical(pos <= m_size);
                return m_data[pos];
            }

            /// @pre !empty() (critical)
            constexpr const_reference front() const noexcept
            {
                JIO_Expects_critical(!empty());
                return m_data[0];
            }

            /// @pre !empty() (critical)
            constexpr const_reference back() const noexcept
            {
                JIO_Expects_critical(!empty());
                return m_data[m_size - 1];
            }

            constexpr operator string_view_type() const noexcept { return {m_data, m_size}; }

            // Comparison looks at the characters only: not at the address, not at what follows.
            // The other operand is any type that converts to a string view, except a pointer or
            // an array. One template, so that no two candidates tie.
            template <string_view_argument<string_view_type> U>
            friend constexpr bool operator==(const Derived &lhs, const U &rhs) noexcept(
                std::is_nothrow_convertible_v<const U &, string_view_type>)
            {
                return string_view_type(lhs) == string_view_type(rhs);
            }

            template <string_view_argument<string_view_type> U>
            friend constexpr auto operator<=>(const Derived &lhs, const U &rhs) noexcept(
                std::is_nothrow_convertible_v<const U &, string_view_type>)
            {
                return string_view_type(lhs) <=> string_view_type(rhs);
            }

        private:
            friend Derived;

            // Only the derived view can build the base: it is not a type of its own. The
            // default is an empty view, which points to an end that can be read and is CharT().
            constexpr string_view_ops() noexcept = default;
            constexpr string_view_ops(const CharT *p, size_type n) noexcept : m_data(p), m_size(n)
            {
            }
            constexpr string_view_ops(const string_view_ops &) noexcept = default;
            constexpr string_view_ops &operator=(const string_view_ops &) noexcept = default;

            // The length up to the first CharT() in [p, p + capacity); capacity.value if there is
            // none or p is null, which the constructors then report.
            static constexpr size_type scan_(const CharT *p, src_capacity_t capacity) noexcept
            {
                if (p == nullptr)
                    return capacity.value;
                const CharT *terminator = Traits::find(p, capacity.value, CharT());
                return terminator == nullptr ? capacity.value
                                             : static_cast<size_type>(terminator - p);
            }

            static constexpr CharT s_terminator = CharT();

            const CharT *m_data = &s_terminator;
            size_type m_size = 0;
        };
    } // namespace detail

    /// One of the string views nd, ns and nt, whatever the character type.
    template <typename V>
    concept string_view_family =
        requires { typename std::remove_cvref_t<V>::string_view_family_tag; };

    template <typename CharT, typename Traits = std::char_traits<CharT>>
    class basic_nd_string_view
        : public detail::string_view_ops<basic_nd_string_view<CharT, Traits>, CharT, Traits>
    {
        using ops_type = detail::string_view_ops<basic_nd_string_view, CharT, Traits>;

    public:
        static constexpr end_guarantee guarantee = end_guarantee::determinable;
        using string_view_type = typename ops_type::string_view_type;
        using size_type = typename ops_type::size_type;
        using shortened_type = basic_nd_string_view;

        /// An empty view; its end is a static CharT().
        constexpr basic_nd_string_view() noexcept = default;

        /// Scans for the terminator in [p, p + capacity) and ends the view there, as for a C
        /// string.
        /// @pre p != nullptr
        /// @pre [p, p + capacity) can be read and contains CharT() (critical)
        explicit constexpr basic_nd_string_view(const CharT *p, src_capacity_t capacity) noexcept
            : ops_type(p, ops_type::scan_(p, capacity))
        {
            JIO_Expects_critical(this->data() != nullptr && this->size() < capacity.value);
        }

        /// The n characters at p, in a buffer that can be read up to capacity: the end is
        /// inside it.
        /// @pre p != nullptr && n < capacity.value (critical)
        /// @pre [p, p + capacity) can be read
        explicit constexpr basic_nd_string_view(const CharT *p, size_type n,
                                                src_capacity_t capacity) noexcept
            : ops_type(p, n)
        {
            JIO_Expects_critical(this->data() != nullptr && this->size() < capacity.value);
        }

        basic_nd_string_view(std::nullptr_t, src_capacity_t) = delete;
        basic_nd_string_view(std::nullptr_t, size_type, src_capacity_t) = delete;

        /// From a type that states an end that can be read. A view of ours is not looked at.
        /// @pre string_view_type(u).data() != nullptr (critical, for types that are not ours)
        template <typename U>
            requires(!std::same_as<std::remove_cvref_t<U>, basic_nd_string_view> &&
                     nd_string_view_like<U> && std::same_as<char_type_t<U>, CharT> &&
                     std::convertible_to<const std::remove_cvref_t<U> &, string_view_type>)
        constexpr basic_nd_string_view(const U &u) noexcept(
            std::is_nothrow_convertible_v<const U &, string_view_type>)
            : ops_type(string_view_type(u).data(), string_view_type(u).size())
        {
            if constexpr (!string_view_family<U>)
                JIO_Expects_critical(this->data() != nullptr);
        }

        /// On the caller's word that the end of `sv` can be read.
        /// @pre sv.data() != nullptr (critical)
        /// @pre sv.data()[sv.size()] can be read
        explicit constexpr basic_nd_string_view(assume_t, string_view_type sv) noexcept
            : ops_type(sv.data(), sv.size())
        {
            JIO_Expects_critical(this->data() != nullptr);
        }

        constexpr basic_nd_string_view(const basic_nd_string_view &) noexcept = default;
        constexpr basic_nd_string_view &operator=(const basic_nd_string_view &) noexcept = default;
    };

    template <typename CharT, typename Traits = std::char_traits<CharT>>
    class basic_ns_string_view
        : public detail::string_view_ops<basic_ns_string_view<CharT, Traits>, CharT, Traits>
    {
        using ops_type = detail::string_view_ops<basic_ns_string_view, CharT, Traits>;

    public:
        static constexpr end_guarantee guarantee = end_guarantee::sentinel;
        using string_view_type = typename ops_type::string_view_type;
        using size_type = typename ops_type::size_type;
        using shortened_type = basic_ns_string_view;

        /// An empty view; its end is a static CharT().
        constexpr basic_ns_string_view() noexcept = default;

        /// Scans for the terminator in [p, p + capacity) and ends the view there, as for a C
        /// string.
        /// @pre p != nullptr
        /// @pre [p, p + capacity) can be read and contains CharT() (critical)
        explicit constexpr basic_ns_string_view(const CharT *p, src_capacity_t capacity) noexcept
            : ops_type(p, ops_type::scan_(p, capacity))
        {
            JIO_Expects_critical(this->data() != nullptr && this->size() < capacity.value);
        }

        /// The n characters at p, in a buffer that can be read up to capacity and has a
        /// terminator at or after the end.
        /// @pre p != nullptr && n < capacity.value (critical)
        /// @pre [p, p + capacity) can be read
        /// @pre [p + n, p + capacity) contains CharT() (audit: finding it is O(capacity))
        explicit constexpr basic_ns_string_view(const CharT *p, size_type n,
                                                src_capacity_t capacity) noexcept
            : ops_type(p, n)
        {
            JIO_Expects_critical(this->data() != nullptr && this->size() < capacity.value);
            JIO_Expects_audit(Traits::find(p + n, capacity.value - n, CharT()) != nullptr);
        }

        basic_ns_string_view(std::nullptr_t, src_capacity_t) = delete;
        basic_ns_string_view(std::nullptr_t, size_type, src_capacity_t) = delete;

        /// From a type that states a terminator at or after the end. A view of ours is not
        /// looked at.
        /// @pre string_view_type(u).data() != nullptr (critical, for types that are not ours)
        template <typename U>
            requires(!std::same_as<std::remove_cvref_t<U>, basic_ns_string_view> &&
                     ns_string_view_like<U> && std::same_as<char_type_t<U>, CharT> &&
                     std::convertible_to<const std::remove_cvref_t<U> &, string_view_type>)
        constexpr basic_ns_string_view(const U &u) noexcept(
            std::is_nothrow_convertible_v<const U &, string_view_type>)
            : ops_type(string_view_type(u).data(), string_view_type(u).size())
        {
            if constexpr (!string_view_family<U>)
                JIO_Expects_critical(this->data() != nullptr);
        }

        /// On the caller's word that a CharT() follows `sv` inside valid memory.
        /// @pre sv.data() != nullptr (critical)
        /// @pre a CharT() at or after sv.data() + sv.size(), reached without leaving the memory
        explicit constexpr basic_ns_string_view(assume_t, string_view_type sv) noexcept
            : ops_type(sv.data(), sv.size())
        {
            JIO_Expects_critical(this->data() != nullptr);
        }

        constexpr basic_ns_string_view(const basic_ns_string_view &) noexcept = default;
        constexpr basic_ns_string_view &operator=(const basic_ns_string_view &) noexcept = default;
    };

    template <typename CharT, typename Traits = std::char_traits<CharT>>
    class basic_nt_string_view
        : public detail::string_view_ops<basic_nt_string_view<CharT, Traits>, CharT, Traits>
    {
        using ops_type = detail::string_view_ops<basic_nt_string_view, CharT, Traits>;

    public:
        static constexpr end_guarantee guarantee = end_guarantee::terminated;
        using string_view_type = typename ops_type::string_view_type;
        using size_type = typename ops_type::size_type;
        using shortened_type = basic_ns_string_view<CharT, Traits>;

        /// An empty view; its terminator is a static CharT().
        constexpr basic_nt_string_view() noexcept = default;

        /// Scans for the terminator in [p, p + capacity) and ends the view there. A terminator
        /// before the end of the buffer is the end of the view, as for a C string.
        /// @pre p != nullptr
        /// @pre [p, p + capacity) can be read and contains CharT() (critical)
        explicit constexpr basic_nt_string_view(const CharT *p, src_capacity_t capacity) noexcept
            : ops_type(p, ops_type::scan_(p, capacity))
        {
            JIO_Expects_critical(this->data() != nullptr && this->size() < capacity.value);
        }

        /// A view of the n characters at p, which are followed by the terminator. The terminator
        /// may be followed by more of the buffer; characters before it may be CharT().
        /// @pre p != nullptr && n < capacity.value (critical)
        /// @pre [p, p + capacity) can be read and p[n] == CharT() (critical)
        explicit constexpr basic_nt_string_view(const CharT *p, size_type n,
                                                src_capacity_t capacity) noexcept
            : ops_type(p, n)
        {
            JIO_Expects_critical(this->data() != nullptr && this->size() < capacity.value);
            JIO_Expects_critical(this->data()[this->size()] == CharT());
        }

        /// From a string literal, or any array that is a constant expression; the array is
        /// scanned as with a capacity of N. An array that is not a constant (a buffer) is
        /// refused at compile time.
        /// @pre the array contains CharT()
        template <std::size_t N>
        consteval basic_nt_string_view(const CharT (&literal)[N]) noexcept
            : basic_nt_string_view(literal, src_capacity(N))
        {
        }

        basic_nt_string_view(std::nullptr_t, src_capacity_t) = delete;
        basic_nt_string_view(std::nullptr_t, size_type, src_capacity_t) = delete;

        /// From a type that states a terminated end (std::basic_string, ...): the pointer comes
        /// from its c_str(), the size from its conversion to a string view. Copying a view of
        /// ours takes the copy constructor and checks nothing.
        /// @pre get_c_str(u)[string_view_type(u).size()] == CharT() (critical)
        template <typename U>
            requires(!std::same_as<std::remove_cvref_t<U>, basic_nt_string_view> &&
                     nt_string_view_like<U> && std::same_as<char_type_t<U>, CharT> &&
                     std::convertible_to<const std::remove_cvref_t<U> &, string_view_type>)
        constexpr basic_nt_string_view(const U &u) noexcept(
            std::is_nothrow_convertible_v<const U &, string_view_type> && noexcept(get_c_str(u)))
            : basic_nt_string_view(get_c_str(u), string_view_type(u).size(),
                                   src_capacity(string_view_type(u).size() + 1))
        {
        }

        /// On the caller's word that the end of `sv` can be read.
        /// @pre sv.data() != nullptr (critical)
        /// @pre sv.data()[sv.size()] can be read and == CharT() (critical)
        explicit constexpr basic_nt_string_view(assume_t, string_view_type sv) noexcept
            : ops_type(sv.data(), sv.size())
        {
            JIO_Expects_critical(this->data() != nullptr);
            JIO_Expects_critical(this->data()[this->size()] == CharT());
        }

        constexpr basic_nt_string_view(const basic_nt_string_view &) noexcept = default;
        constexpr basic_nt_string_view &operator=(const basic_nt_string_view &) noexcept = default;

        /// The characters up to the terminator, which follows them.
        constexpr const CharT *c_str() const noexcept { return this->data(); }
    };

    // --- free functions on all the views ------------------------------------------------------
    // They build the new view with the tag assume, which for ns and nd costs a null check and
    // for nt one more look at the terminator. The guarantee they rely on is the one of the view
    // they are given, so they are sound.

    /// The rest of the view from pos on: the end does not move, so the guarantee stays.
    /// @pre pos <= view.size() (critical)
    template <string_view_family V>
    [[nodiscard]] constexpr V substr(const V &view, std::size_t pos = 0) noexcept
    {
        JIO_Expects_critical(pos <= view.size());
        return V(assume, typename V::string_view_type(view.data() + pos, view.size() - pos));
    }

    /// The view without its first n characters; same as substr(view, n).
    /// @pre n <= view.size() (critical)
    template <string_view_family V>
    [[nodiscard]] constexpr V rm_prefix(const V &view, std::size_t n) noexcept
    {
        return substr(view, n);
    }

    /// n characters from pos on. The end moves back, so an nt view gives an ns view.
    /// @pre pos <= view.size() && n <= view.size() - pos (critical)
    template <string_view_family V>
    [[nodiscard]] constexpr typename V::shortened_type substr(const V &view, std::size_t pos,
                                                              std::size_t n) noexcept
    {
        JIO_Expects_critical(pos <= view.size() && n <= view.size() - pos);
        return
            typename V::shortened_type(assume, typename V::string_view_type(view.data() + pos, n));
    }

    /// The view without its last n characters.
    /// @pre n <= view.size() (critical)
    template <string_view_family V>
    [[nodiscard]] constexpr typename V::shortened_type rm_suffix(const V &view,
                                                                 std::size_t n) noexcept
    {
        JIO_Expects_critical(n <= view.size());
        return typename V::shortened_type(
            assume, typename V::string_view_type(view.data(), view.size() - n));
    }

    /// Whether the view ends at a terminator: the end of every view of ours can be read.
    template <string_view_family V>
    [[nodiscard]] constexpr bool is_terminated(const V &view) noexcept
    {
        return view.data()[view.size()] == typename V::value_type();
    }

    /// The view as a terminated one, if its end is the terminator.
    template <string_view_family V>
    [[nodiscard]] constexpr std::optional<
        basic_nt_string_view<typename V::value_type, typename V::traits_type>>
    as_nt(const V &view) noexcept
    {
        if (!is_terminated(view))
            return std::nullopt;
        return basic_nt_string_view<typename V::value_type, typename V::traits_type>(
            assume, typename V::string_view_type(view.data(), view.size()));
    }

    // Shortcuts. This project uses only char; the others are there for whoever needs them.
    using nd_string_view = basic_nd_string_view<char>;
    using nd_wstring_view = basic_nd_string_view<wchar_t>;
    using nd_u8string_view = basic_nd_string_view<char8_t>;
    using nd_u16string_view = basic_nd_string_view<char16_t>;
    using nd_u32string_view = basic_nd_string_view<char32_t>;
    using ns_string_view = basic_ns_string_view<char>;
    using ns_wstring_view = basic_ns_string_view<wchar_t>;
    using ns_u8string_view = basic_ns_string_view<char8_t>;
    using ns_u16string_view = basic_ns_string_view<char16_t>;
    using ns_u32string_view = basic_ns_string_view<char32_t>;
    using nt_string_view = basic_nt_string_view<char>;
    using nt_wstring_view = basic_nt_string_view<wchar_t>;
    using nt_u8string_view = basic_nt_string_view<char8_t>;
    using nt_u16string_view = basic_nt_string_view<char16_t>;
    using nt_u32string_view = basic_nt_string_view<char32_t>;

    /// A string literal as an nt_string_view: `constexpr auto s = literal("text");`. Unlike
    /// `auto s = "text"`, the type is not a pointer.
    template <typename CharT, std::size_t N>
    [[nodiscard]] consteval basic_nt_string_view<CharT> literal(const CharT (&text)[N]) noexcept
    {
        return basic_nt_string_view<CharT>(text);
    }

    inline namespace literals
    {
        /// `"text"_nt`, the same as literal("text").
        [[nodiscard]] consteval nt_string_view operator""_nt(const char *text,
                                                              std::size_t length) noexcept
        {
            return nt_string_view(text, src_capacity(length + 1));
        }
    } // namespace literals
} // namespace jetio

// A view is cheap to copy, and its iterators outlive it: it is a view and a borrowed range.
template <jetio::string_view_family V> inline constexpr bool std::ranges::enable_view<V> = true;
template <jetio::string_view_family V>
inline constexpr bool std::ranges::enable_borrowed_range<V> = true;

// The hash of a view is the one of the std::basic_string_view with the same characters.
template <jetio::string_view_family V> struct std::hash<V>
{
    std::size_t operator()(const V &view) const noexcept
    {
        return std::hash<typename V::string_view_type>{}(view);
    }
};
