#pragma once

// Forward declarations of the string views, like <iosfwd>. The default template argument is
// given here only.

#include <string_view>

namespace jetio
{
    template <typename CharT, typename Traits = std::char_traits<CharT>> class basic_nd_string_view;

    template <typename CharT, typename Traits = std::char_traits<CharT>> class basic_ns_string_view;

    template <typename CharT, typename Traits = std::char_traits<CharT>> class basic_nt_string_view;

    using nd_string_view = basic_nd_string_view<char>;
    using ns_string_view = basic_ns_string_view<char>;
    using nt_string_view = basic_nt_string_view<char>;
} // namespace jetio
