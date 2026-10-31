#pragma once

// Output of basic_nt_string_view: operator<< and std::format. Apart from nt_string_view.hpp so
// that the view does not depend on the streams, and because <format> is heavy.

#include <jetio/std/nt_string_view.hpp>

#include <format>
#include <ostream>
#include <string_view>

namespace jetio
{
    template <typename CharT, typename Traits>
    std::basic_ostream<CharT, Traits> &operator<<(std::basic_ostream<CharT, Traits> &os,
                                                  const basic_nt_string_view<CharT, Traits> &view)
    {
        return os << std::basic_string_view<CharT, Traits>(view);
    }
} // namespace jetio

template <typename CharT, typename Traits>
struct std::formatter<jetio::basic_nt_string_view<CharT, Traits>, CharT>
    : std::formatter<std::basic_string_view<CharT, Traits>, CharT>
{
    template <typename FormatContext>
    auto format(const jetio::basic_nt_string_view<CharT, Traits> &view, FormatContext &ctx) const
    {
        return std::formatter<std::basic_string_view<CharT, Traits>, CharT>::format(
            std::basic_string_view<CharT, Traits>(view), ctx);
    }
};
