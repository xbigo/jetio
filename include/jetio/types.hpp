#pragma once

#include <jetio/std/contract.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace jetio
{
    // Sizes and offsets of devices are 64 bit on every platform, so that 32 bit targets can
    // address large files. Conversion to the platform's size_t is explicit.
    using size64_t = std::uint64_t;
    using offset64_t = std::int64_t;

    /// Half-open range [begin, end) of device offsets.
    struct offset64_range
    {
        offset64_t begin{0};
        offset64_t end{0};

        friend constexpr bool operator==(const offset64_range &,
                                         const offset64_range &) noexcept = default;
    };

    /// Marks an offset or size that is not known, or "up to the end" as the end of a range.
    inline constexpr offset64_t unknown_offset = std::numeric_limits<offset64_t>::max();
    inline constexpr size64_t unknown_size = static_cast<size64_t>(unknown_offset);

    /// @return true if begin is not negative and either end is unknown_offset or
    ///         begin <= end (and end is not negative)
    constexpr bool is_valid_range(offset64_range range) noexcept
    {
        return range.begin >= 0 &&
               (range.end == unknown_offset || (range.end >= 0 && range.begin <= range.end));
    }

    /// @return true if the range is valid and, unless its end is unknown, within [0, size]
    constexpr bool is_valid_range(offset64_range range, size64_t size) noexcept
    {
        return is_valid_range(range) &&
               (range.end == unknown_offset || std::cmp_less_equal(range.end, size));
    }

    /// @pre is_valid_range(range)
    /// @return unknown_size if the end of the range is unknown, otherwise end - begin
    constexpr size64_t range_size(offset64_range range) noexcept
    {
        JIO_Expects(is_valid_range(range));
        return range.end == unknown_offset ? unknown_size
                                           : static_cast<size64_t>(range.end - range.begin);
    }

    /// @return true if n can be represented as std::size_t on this platform
    constexpr bool in_size_t_range(size64_t n) noexcept
    {
        return std::in_range<std::size_t>(n);
    }
    constexpr bool in_size_t_range(offset64_t n) noexcept
    {
        return std::in_range<std::size_t>(n);
    }

    /// @pre in_size_t_range(n)
    constexpr std::size_t to_size_t(size64_t n) noexcept
    {
        JIO_Expects_critical(in_size_t_range(n));
        return static_cast<std::size_t>(n);
    }

    /// @pre in_size_t_range(n)
    constexpr std::size_t offset_to_size_t(offset64_t n) noexcept
    {
        JIO_Expects_critical(in_size_t_range(n));
        return static_cast<std::size_t>(n);
    }

    /// @pre std::in_range<std::ptrdiff_t>(n)
    constexpr std::ptrdiff_t to_ptrdiff_t(offset64_t n) noexcept
    {
        JIO_Expects_critical(std::in_range<std::ptrdiff_t>(n));
        return static_cast<std::ptrdiff_t>(n);
    }
} // namespace jetio
