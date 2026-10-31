#pragma once

// A string view whose end is a terminator: data()[size()] == CharT(). The terminator is not
// part of the view, and the characters before it may contain CharT() too.
//
// Although the implementation supports several character types, this project uses only char,
// UTF-8 included: strings inside the project are UTF-8 in char, and another encoding is
// converted only at the boundary of an integration.
//
// Design: see docs/design/string-views.md, including how it differs from std::string_view.
//  - Immutable: no member changes a view in place. Operations return new views; only
//    assignment and swap replace the whole value.
//  - Strict in: the invariant is established when the view is built, never checked again.
//    Nothing that reaches the view as a pointer is trusted without an explicit source
//    capacity, and there is no implicit conversion from a pointer or an array.
//  - Primitives, which need the private members, have a meaning no other member covers, or
//    are needed by the standard algorithms: the constructors, data(), size(), c_str() (the
//    terminator), the iterator functions, swap(). The extensions (empty, operator[], front,
//    back, the conversion, the comparison) are composed from them. Everything else, substr
//    and rm_prefix included, is a free function.
//  - There is no searching or matching; use std::string_view(view) or an external algorithm.
//  - Input and output (<<, std::format) are in nt_string_view_io.hpp, so that this header does
//    not depend on the streams.

#include <jetio/std/contract.hpp>
#include <jetio/std/string_view_fwd.hpp>
#include <jetio/std/string_view_guarantee.hpp>

#include <compare>
#include <concepts>
#include <cstddef>
#include <functional>
#include <iterator>
#include <ranges>
#include <string_view>
#include <type_traits>

namespace jetio
{
    template <typename CharT, typename Traits> class basic_nt_string_view
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

        // --- primitives -------------------------------------------------------------------

        /// An empty view; its terminator is a static CharT().
        constexpr basic_nt_string_view() noexcept = default;

        /// Scans for the terminator in [p, p + capacity) and ends the view there. A terminator
        /// before the end of the buffer is the end of the view, as for a C string.
        /// @pre p != nullptr
        /// @pre [p, p + capacity) can be read and contains CharT() (critical)
        explicit constexpr basic_nt_string_view(const CharT *p, src_capacity_t capacity)
            : m_data(p), m_size(scan_(p, capacity))
        {
            JIO_Expects_critical(m_data != nullptr && m_size < capacity.value);
        }

        /// A view of the n characters at p, which are followed by the terminator. The terminator
        /// may be followed by more of the buffer; characters before it may be CharT().
        /// @pre p != nullptr && n < capacity.value (critical)
        /// @pre [p, p + capacity) can be read and p[n] == CharT() (critical)
        explicit constexpr basic_nt_string_view(const CharT *p, size_type n,
                                                src_capacity_t capacity)
            : m_data(p), m_size(n)
        {
            JIO_Expects_critical(m_data != nullptr && m_size < capacity.value);
            JIO_Expects_critical(m_data[m_size] == CharT());
        }

        basic_nt_string_view(std::nullptr_t, src_capacity_t) = delete;
        basic_nt_string_view(std::nullptr_t, size_type, src_capacity_t) = delete;

        /// From a type that states a terminated end (std::basic_string, ...): the pointer comes
        /// from its c_str(), the size from its conversion to a string view. Copying a
        /// basic_nt_string_view takes the copy constructor and checks nothing.
        /// @pre get_c_str(u)[string_view_type(u).size()] == CharT() (critical)
        template <typename U>
            requires(!std::same_as<std::remove_cvref_t<U>, basic_nt_string_view> &&
                     nt_string_view_like<U> && std::same_as<char_type_t<U>, CharT> &&
                     std::convertible_to<const std::remove_cvref_t<U> &, string_view_type>)
        constexpr basic_nt_string_view(const U &u)
            : basic_nt_string_view(get_c_str(u), string_view_type(u).size(),
                                   src_capacity(string_view_type(u).size() + 1))
        {
        }

        constexpr basic_nt_string_view(const basic_nt_string_view &) noexcept = default;
        constexpr basic_nt_string_view &operator=(const basic_nt_string_view &) noexcept = default;

        constexpr const_pointer data() const noexcept { return m_data; }
        constexpr size_type size() const noexcept { return m_size; }

        /// The characters up to the terminator, which follows them.
        constexpr const_pointer c_str() const noexcept { return data(); }

        constexpr void swap(basic_nt_string_view &other) noexcept
        {
            const basic_nt_string_view tmp = *this;
            *this = other;
            other = tmp;
        }

        constexpr const_iterator begin() const noexcept { return data(); }
        constexpr const_iterator end() const noexcept { return data() + size(); }
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

        // --- extensions -------------------------------------------------------------------

        constexpr bool empty() const noexcept { return size() == 0; }

        /// pos == size() is allowed: it is the terminator.
        /// @pre pos <= size() (critical)
        constexpr const_reference operator[](size_type pos) const
        {
            JIO_Expects_critical(pos <= size());
            return data()[pos];
        }

        /// @pre not empty (critical)
        constexpr const_reference front() const
        {
            JIO_Expects_critical(!empty());
            return data()[0];
        }

        /// @pre not empty (critical)
        constexpr const_reference back() const
        {
            JIO_Expects_critical(!empty());
            return data()[size() - 1];
        }

        constexpr operator string_view_type() const noexcept { return {data(), size()}; }

        // Comparison looks at the characters only: not at the address, not at the terminator.
        // The other operand is any type that converts to a string view, except a pointer or an
        // array. One template, so that no two candidates tie.
        template <detail::string_view_argument<string_view_type> U>
        friend constexpr bool operator==(const basic_nt_string_view &lhs, const U &rhs) noexcept(
            std::is_nothrow_convertible_v<const U &, string_view_type>)
        {
            return string_view_type(lhs) == string_view_type(rhs);
        }

        template <detail::string_view_argument<string_view_type> U>
        friend constexpr auto operator<=>(const basic_nt_string_view &lhs, const U &rhs) noexcept(
            std::is_nothrow_convertible_v<const U &, string_view_type>)
        {
            return string_view_type(lhs) <=> string_view_type(rhs);
        }

    private:
        // The length up to the first terminator; capacity.value if there is none or p is null,
        // which the constructor then reports.
        static constexpr size_type scan_(const CharT *p, src_capacity_t capacity) noexcept
        {
            if (p == nullptr)
                return capacity.value;
            const CharT *terminator = Traits::find(p, capacity.value, CharT());
            return terminator == nullptr ? capacity.value : static_cast<size_type>(terminator - p);
        }

        static constexpr CharT s_terminator = CharT();

        const CharT *m_data = &s_terminator;
        size_type m_size = 0;
    };

    /// The rest of a view from pos on; it ends at the same terminator. Builds the view through
    /// the public constructor, which looks at the terminator once more.
    /// @pre pos <= view.size() (critical)
    template <typename CharT, typename Traits>
    [[nodiscard]] constexpr basic_nt_string_view<CharT, Traits>
    substr(const basic_nt_string_view<CharT, Traits> &view, std::size_t pos = 0)
    {
        JIO_Expects_critical(pos <= view.size());
        const std::size_t rest = view.size() - pos;
        return basic_nt_string_view<CharT, Traits>(view.data() + pos, rest, src_capacity(rest + 1));
    }

    /// The view without its first n characters; it does not change `view`. Same as substr.
    /// @pre n <= view.size() (critical)
    template <typename CharT, typename Traits>
    [[nodiscard]] constexpr basic_nt_string_view<CharT, Traits>
    rm_prefix(const basic_nt_string_view<CharT, Traits> &view, std::size_t n)
    {
        return substr(view, n);
    }

    template <typename CharT, typename Traits>
    inline constexpr end_guarantee end_guarantee_of<basic_nt_string_view<CharT, Traits>> =
        end_guarantee::terminated;
} // namespace jetio

// A view is cheap to copy, and its iterators outlive it: it is a view and a borrowed range.
template <typename CharT, typename Traits>
inline constexpr bool std::ranges::enable_view<jetio::basic_nt_string_view<CharT, Traits>> = true;

template <typename CharT, typename Traits>
inline constexpr bool
    std::ranges::enable_borrowed_range<jetio::basic_nt_string_view<CharT, Traits>> = true;

template <typename CharT, typename Traits>
struct std::hash<jetio::basic_nt_string_view<CharT, Traits>>
{
    std::size_t operator()(const jetio::basic_nt_string_view<CharT, Traits> &view) const noexcept
    {
        return std::hash<std::basic_string_view<CharT, Traits>>{}(view);
    }
};
