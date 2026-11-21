#pragma once

// String algorithms that the string views and std::string_view do not have. Searching is the
// one of std::string_view, which every string and view converts to.
//
// The case functions and the case insensitive comparison are for ASCII only: the letters A to Z
// and a to z. Other bytes, those of the multi-byte characters of UTF-8 too, are kept as they are.

#include <jio/std/contract.hpp>
#include <jio/std/string_view_concepts.hpp>
#include <jio/string_algo/octet.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

namespace jio::str_algo
{
    namespace detail
    {
        /// Text in char, as UTF-8 is held.
        template <typename T>
        concept char_source = string_view_source<T> && std::same_as<char_type_t<T>, char>;

        constexpr std::size_t table_index(char c) noexcept
        {
            return static_cast<unsigned char>(c);
        }

        /// What every value of char becomes: itself, except the 26 letters that begin with
        /// `from`, which become the 26 that begin with `to`.
        constexpr std::array<char, 256> make_case_table(char from, char to) noexcept
        {
            std::array<char, 256> table{};
            for (std::size_t i = 0; i < table.size(); ++i)
                table[i] = static_cast<char>(i);
            for (int i = 0; i < 26; ++i, ++from, ++to)
                table[table_index(from)] = to;
            return table;
        }

        inline constexpr std::array<char, 256> upper_table = make_case_table('a', 'A');
        inline constexpr std::array<char, 256> lower_table = make_case_table('A', 'a');
    } // namespace detail

    /// @return the character in upper case if it is a letter of a to z, else itself
    [[nodiscard]] constexpr char ascii_to_upper(char c) noexcept
    {
        return detail::upper_table[detail::table_index(c)];
    }

    /// @return the character in lower case if it is a letter of A to Z, else itself
    [[nodiscard]] constexpr char ascii_to_lower(char c) noexcept
    {
        return detail::lower_table[detail::table_index(c)];
    }

    /// Equality of two characters that ignores the case of ASCII letters, for the algorithms of
    /// the standard library.
    struct icase_equal
    {
        constexpr bool operator()(char lhs, char rhs) const noexcept
        {
            return ascii_to_lower(lhs) == ascii_to_lower(rhs);
        }
    };

    /// Order of two characters that ignores the case of ASCII letters.
    struct icase_less
    {
        constexpr bool operator()(char lhs, char rhs) const noexcept
        {
            return static_cast<unsigned char>(ascii_to_lower(lhs)) <
                   static_cast<unsigned char>(ascii_to_lower(rhs));
        }
    };

    /// Negative if lhs is before rhs, zero if they are equal, else positive; ignores the case of
    /// ASCII letters.
    struct icase_compare
    {
        constexpr int operator()(char lhs, char rhs) const noexcept
        {
            return static_cast<int>(static_cast<unsigned char>(ascii_to_lower(lhs))) -
                   static_cast<int>(static_cast<unsigned char>(ascii_to_lower(rhs)));
        }
    };

    /// Appends the text with the ASCII letters in upper case to `out`.
    void ascii_to_upper(const detail::char_source auto &text, char_string auto &out)
    {
        std::ranges::transform(std::string_view(text), std::back_inserter(out),
                               [](char c) { return ascii_to_upper(c); });
    }

    /// Appends the text with the ASCII letters in lower case to `out`.
    void ascii_to_lower(const detail::char_source auto &text, char_string auto &out)
    {
        std::ranges::transform(std::string_view(text), std::back_inserter(out),
                               [](char c) { return ascii_to_lower(c); });
    }

    /// Appends the text with every `from` replaced by `to` to `out`.
    void replace(const detail::char_source auto &text, char from, char to, char_string auto &out)
    {
        std::ranges::replace_copy(std::string_view(text), std::back_inserter(out), from, to);
    }

    /// Writes the text with every occurrence of the pattern replaced, searched from the left;
    /// what was put in is not searched again. The algorithm of the standard library kind, which
    /// the function below wraps.
    /// @pre the pattern is not empty (critical)
    /// @return the output iterator after the text
    auto replace_copy(const detail::char_source auto &text, const detail::char_source auto &pattern,
                      const detail::char_source auto &with, std::output_iterator<char> auto out)
    {
        const std::string_view whole(text);
        const std::string_view old(pattern);
        const std::string_view fresh(with);
        JIO_Expects_critical(!old.empty());
        std::size_t from = 0;
        for (std::size_t hit; (hit = whole.find(old, from)) != std::string_view::npos;
             from = hit + old.size())
        {
            out = std::ranges::copy(whole.substr(from, hit - from), std::move(out)).out;
            out = std::ranges::copy(fresh, std::move(out)).out;
        }
        return std::ranges::copy(whole.substr(from), std::move(out)).out;
    }

    /// Appends the text with every occurrence of the pattern replaced to `out`: replace_copy
    /// through a back inserter.
    /// @pre the pattern is not empty (critical)
    void replace(const detail::char_source auto &text, const detail::char_source auto &pattern,
                 const detail::char_source auto &with, char_string auto &out)
    {
        replace_copy(text, pattern, with, std::back_inserter(out));
    }
} // namespace jio::str_algo
