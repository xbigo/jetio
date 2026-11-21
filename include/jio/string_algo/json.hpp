#pragma once

// Escaping of text for JSON strings. A control character below 0x20 becomes \u00xx, a quote and
// a backslash get a backslash; everything else, bytes of UTF-8 included, is kept as it is.

#include <jio/string_algo/octet.hpp>

#include <cstddef>
#include <iterator>
#include <ranges>
#include <string>
#include <string_view>

namespace jio::json
{
    /// The escaped characters, without the quotes around them.
    /// @return the output iterator after them
    constexpr auto escape(octet_range auto &&in, std::output_iterator<char> auto out)
    {
        constexpr std::string_view digits = "0123456789abcdef";
        for (const auto &byte : in)
        {
            const unsigned char c = to_octet(byte);
            if (c < 0x20)
            {
                for (const char x : std::string_view("\\u00"))
                    *out++ = x;
                *out++ = digits[c >> 4];
                *out++ = digits[c & 0xf];
            }
            else
            {
                if (c == '"' || c == '\\')
                    *out++ = '\\';
                *out++ = static_cast<char>(c);
            }
        }
        return out;
    }

    /// Appends the string for JSON to `out`: the escaped characters between quotes.
    void escape_string(const octet_range auto &in, char_string auto &out)
    {
        out.push_back('"');
        escape(in, std::back_inserter(out));
        out.push_back('"');
    }
} // namespace jio::json
