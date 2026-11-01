#pragma once

// Output of the string views: operator<< and std::format. Apart from the views themselves so
// that they do not depend on the streams, and because <format> is heavy.

#include <jetio/std/string_view.hpp>

#include <concepts>
#include <format>
#include <ostream>
#include <string_view>

namespace jetio
{
    template <typename CharT, typename Traits, string_view_family V>
        requires(std::same_as<typename V::value_type, CharT> &&
                 std::same_as<typename V::traits_type, Traits>)
    std::basic_ostream<CharT, Traits> &operator<<(std::basic_ostream<CharT, Traits> &os,
                                                  const V &view)
    {
        return os << std::basic_string_view<CharT, Traits>(view);
    }
} // namespace jetio

// The pattern View<CharT, Traits> is more specific than the standard's formatter for any range,
// which our views are too; without it the two would be ambiguous.
template <template <typename, typename> class View, typename CharT, typename Traits>
    requires jetio::string_view_family<View<CharT, Traits>>
struct std::formatter<View<CharT, Traits>, CharT>
    : std::formatter<std::basic_string_view<CharT, Traits>, CharT>
{
    template <typename FormatContext>
    auto format(const View<CharT, Traits> &view, FormatContext &ctx) const
    {
        using base = std::formatter<std::basic_string_view<CharT, Traits>, CharT>;
        return base::format(std::basic_string_view<CharT, Traits>(view), ctx);
    }
};
