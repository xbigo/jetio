#pragma once

// Output of jio::string: operator<< and std::format, apart from the string for the reason that
// string_view_io.hpp is apart from the views.

#include <jio/std/string.hpp>

#include <format>
#include <ostream>
#include <string_view>

namespace jio
{
    template <typename CharT, typename Traits>
    std::basic_ostream<CharT, Traits> &operator<<(std::basic_ostream<CharT, Traits> &os,
                                                  const basic_string<CharT, Traits> &text)
    {
        return os << std::basic_string_view<CharT, Traits>(text);
    }
} // namespace jio

// More specialized than the standard's formatter for any range, which a string is as well.
template <typename CharT, typename Traits>
struct std::formatter<jio::basic_string<CharT, Traits>, CharT>
    : std::formatter<std::basic_string_view<CharT, Traits>, CharT>
{
    template <typename FormatContext>
    auto format(const jio::basic_string<CharT, Traits> &text, FormatContext &ctx) const
    {
        using base = std::formatter<std::basic_string_view<CharT, Traits>, CharT>;
        return base::format(std::basic_string_view<CharT, Traits>(text), ctx);
    }
};
