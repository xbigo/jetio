#pragma once

// A string view whose end is a terminator: data()[size()] == CharT(). The terminator is not
// part of the view, and the characters before it may contain CharT() too.
//
// The invariant is established when the view is built and never checked again. Nothing that
// reaches the view as a pointer is trusted without an explicit source capacity, and there is
// no implicit conversion from a pointer or an array. A pointer or an array is not compared
// either; compare with a std::basic_string_view instead.

#include <jetio/std/contract.hpp>
#include <jetio/std/string_view_guarantee.hpp>

#include <compare>
#include <concepts>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>

namespace jetio
{
    template <typename CharT, typename Traits = std::char_traits<CharT>> class basic_nt_string_view
    {
    public:
        using traits_type = Traits;
        using value_type = CharT;
        using size_type = std::size_t;
        using const_pointer = const CharT *;
        using const_reference = const CharT &;
        using const_iterator = const CharT *;
        using iterator = const_iterator;
        using string_view_type = std::basic_string_view<CharT, Traits>;

        /// An empty view; its terminator is a static CharT().
        constexpr basic_nt_string_view() noexcept : m_data(&s_terminator), m_size(0) {}

        /// Scans for the terminator in [p, p + capacity) and ends the view there. A terminator
        /// before the end of the buffer is the end of the view, as for a C string.
        /// @pre p is not null
        /// @pre [p, p + capacity) can be read, and contains CharT() (critical)
        explicit constexpr basic_nt_string_view(const CharT *p, src_capacity_t capacity)
        {
            JIO_Expects_critical(p != nullptr);
            const CharT *terminator = Traits::find(p, capacity.value, CharT());
            JIO_Expects_critical(terminator != nullptr);
            m_data = p;
            m_size = static_cast<size_type>(terminator - p);
        }

        /// A view of the n characters at p, which are followed by the terminator. The terminator
        /// may be followed by more of the buffer; characters before it may be CharT().
        /// @pre p is not null
        /// @pre [p, p + capacity) can be read, n < capacity and p[n] == CharT() (critical)
        explicit constexpr basic_nt_string_view(const CharT *p, size_type n,
                                                src_capacity_t capacity)
        {
            JIO_Expects_critical(p != nullptr && n < capacity.value);
            JIO_Expects_critical(p[n] == CharT());
            m_data = p;
            m_size = n;
        }

        basic_nt_string_view(std::nullptr_t, src_capacity_t) = delete;
        basic_nt_string_view(std::nullptr_t, size_type, src_capacity_t) = delete;

        /// From a type that states a terminated end (std::basic_string, ...): the pointer comes
        /// from its c_str(), the size from its conversion to a string view. Copying a
        /// basic_nt_string_view takes the copy constructor and checks nothing.
        /// @pre the type keeps its promise: c_str()[size] == CharT() (critical)
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

        /// The characters up to the terminator, which follows them.
        constexpr const_pointer c_str() const noexcept { return m_data; }

        constexpr size_type size() const noexcept { return m_size; }
        constexpr size_type length() const noexcept { return m_size; }
        constexpr bool empty() const noexcept { return m_size == 0; }

        constexpr const_iterator begin() const noexcept { return m_data; }
        constexpr const_iterator end() const noexcept { return m_data + m_size; }
        constexpr const_iterator cbegin() const noexcept { return begin(); }
        constexpr const_iterator cend() const noexcept { return end(); }

        /// pos == size() is allowed: it is the terminator.
        /// @pre pos <= size()
        constexpr const_reference operator[](size_type pos) const
        {
            JIO_Expects(pos <= m_size);
            return m_data[pos];
        }

        constexpr operator string_view_type() const noexcept { return {m_data, m_size}; }

        // Comparison looks at the characters only: not at the address, not at the terminator.
        // The other operand is any type that converts to a string view, except a pointer or an
        // array: those are not compared. One template, so that no two candidates tie.
        template <typename U>
            requires(!std::is_pointer_v<std::decay_t<U>> &&
                     std::convertible_to<const U &, string_view_type>)
        friend constexpr bool operator==(const basic_nt_string_view &lhs, const U &rhs) noexcept(
            std::is_nothrow_convertible_v<const U &, string_view_type>)
        {
            return string_view_type(lhs) == string_view_type(rhs);
        }

        template <typename U>
            requires(!std::is_pointer_v<std::decay_t<U>> &&
                     std::convertible_to<const U &, string_view_type>)
        friend constexpr auto operator<=>(const basic_nt_string_view &lhs, const U &rhs) noexcept(
            std::is_nothrow_convertible_v<const U &, string_view_type>)
        {
            return string_view_type(lhs) <=> string_view_type(rhs);
        }

    private:
        static constexpr CharT s_terminator = CharT();

        const CharT *m_data;
        size_type m_size;
    };

    using nt_string_view = basic_nt_string_view<char>;
    using nt_wstring_view = basic_nt_string_view<wchar_t>;
    using nt_u8string_view = basic_nt_string_view<char8_t>;
    using nt_u16string_view = basic_nt_string_view<char16_t>;
    using nt_u32string_view = basic_nt_string_view<char32_t>;

    template <typename CharT, typename Traits>
    inline constexpr end_guarantee end_guarantee_of<basic_nt_string_view<CharT, Traits>> =
        end_guarantee::terminated;
} // namespace jetio

template <typename CharT, typename Traits>
struct std::hash<jetio::basic_nt_string_view<CharT, Traits>>
{
    std::size_t operator()(const jetio::basic_nt_string_view<CharT, Traits> &view) const noexcept
    {
        return std::hash<std::basic_string_view<CharT, Traits>>{}(view);
    }
};
