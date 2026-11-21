#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <ranges>
#include <string>
#include <type_traits>

namespace jio
{
    /// A character or a byte: one octet of text or data, as the algorithms of string_algo read it.
    template <typename T>
    concept octet = sizeof(T) == 1 && !std::same_as<std::remove_cv_t<T>, bool> &&
                    (std::integral<T> || std::same_as<std::remove_cv_t<T>, std::byte>);

    /// @return the value of the octet, 0 to 255
    constexpr unsigned char to_octet(octet auto value) noexcept
    {
        if constexpr (std::same_as<decltype(value), std::byte>)
            return std::to_integer<unsigned char>(value);
        else
            return static_cast<unsigned char>(value);
    }

    /// An input range of octets.
    template <typename R>
    concept octet_range = std::ranges::input_range<R> && octet<std::ranges::range_value_t<R>>;

    /// A range of octets that can be read more than once.
    template <typename R>
    concept octet_forward_range =
        std::ranges::forward_range<R> && octet<std::ranges::range_value_t<R>>;

    /// A range of octets that can be walked from both ends: the last element is reached from `end`.
    template <typename R>
    concept octet_bidirectional_range =
        std::ranges::bidirectional_range<R> && std::ranges::common_range<R> &&
        octet<std::ranges::range_value_t<R>>;

    /// What the conversions of string_algo that work on ranges return: where the input stopped (an
    /// iterator into the range, or std::ranges::dangling if the range is a temporary) and the
    /// output iterator after what was written.
    template <typename R, typename Out>
    using convert_result = std::ranges::in_out_result<std::ranges::borrowed_iterator_t<R>, Out>;

    /// A std::basic_string of char, whatever its traits and allocator (std::pmr::string too): what
    /// the functions of string_algo that make text append to.
    template <typename S>
    concept char_string =
        std::same_as<S,
                     std::basic_string<char, typename S::traits_type, typename S::allocator_type>>;
} // namespace jio
