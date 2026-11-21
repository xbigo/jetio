#pragma once

// Hexadecimal text of bytes. Decoding takes both cases.
//
// The algorithm on ranges does not report: it converts to the first thing that is not a digit
// and returns the position it stopped at, so the input was processed if the position is the end.
// Where the number of digits is odd, the last one is left out, so the position is never the end.
// The functions that append to a container decide from the position and report as in error.hpp:
// with the error code if there is one, else thrown as hex::bad_hex_input (invalid argument); if
// they fail the container holds what was converted before the failure, which is all they promise.

#include <jio/error.hpp>
#include <jio/string_algo/octet.hpp>

#include <array>
#include <cstddef>
#include <iterator>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace jio::hex
{
    struct bad_hex_input : exception
    {
        using exception::exception;
    };

    enum class letter_case
    {
        lower,
        upper
    };

    namespace detail
    {
        inline constexpr unsigned char not_a_digit = 0xff;

        /// The value of every byte as a hexadecimal digit of either case, or not_a_digit.
        constexpr std::array<unsigned char, 256> make_decode_table() noexcept
        {
            std::array<unsigned char, 256> table{};
            table.fill(not_a_digit);
            unsigned char value = 0;
            for (unsigned char c = '0'; c <= '9'; ++c, ++value)
                table[c] = value;
            for (unsigned char c = 'a'; c <= 'f'; ++c, ++value)
                table[c] = value;
            value = 10;
            for (unsigned char c = 'A'; c <= 'F'; ++c, ++value)
                table[c] = value;
            return table;
        }

        inline constexpr std::array<unsigned char, 256> decode_table = make_decode_table();
    } // namespace detail

    /// @return the value of a hexadecimal digit of either case, or -1
    [[nodiscard]] constexpr int digit_value(unsigned char c) noexcept
    {
        const unsigned char value = detail::decode_table[c];
        return value == detail::not_a_digit ? -1 : value;
    }

    /// Two digits for every byte.
    /// @return the output iterator after them
    constexpr auto encode(octet_range auto &&in, std::output_iterator<char> auto out,
                          letter_case letters = letter_case::lower)
    {
        const std::string_view digits =
            letters == letter_case::lower ? "0123456789abcdef" : "0123456789ABCDEF";
        for (const auto &byte : in)
        {
            const unsigned char value = to_octet(byte);
            *out++ = digits[value >> 4];
            *out++ = digits[value & 0xf];
        }
        return out;
    }

    /// The bytes of the digits, up to the first pair that is not two digits.
    /// @return where the input stopped, which is the end only if all of it was converted (not if
    ///         the number of digits is odd), and the output iterator after the bytes written
    template <octet_bidirectional_range Range, std::output_iterator<std::byte> Out>
    convert_result<Range, Out> decode(Range &&in, Out out)
    {
        auto first = std::ranges::begin(in);
        auto last = std::ranges::end(in);
        if ((std::ranges::distance(first, last) & 1) != 0)
            --last; // the digit that has no partner is not converted
        while (first != last)
        {
            const unsigned high = detail::decode_table[to_octet(*first++)];
            const unsigned low = detail::decode_table[to_octet(*first++)];
            if (high == detail::not_a_digit || low == detail::not_a_digit)
            {
                std::ranges::advance(first, -2); // back to the pair
                break;
            }
            *out++ = static_cast<std::byte>((high << 4) | low);
        }
        return {std::move(first), std::move(out)};
    }

    /// Appends the text of the bytes, twice as long, to `out`.
    void encode_string(const octet_range auto &in, char_string auto &out,
                       letter_case letters = letter_case::lower)
    {
        encode(in, std::back_inserter(out), letters);
    }

    /// Appends the bytes of the digits to `out`. If it fails, `out` is valid and holds the bytes
    /// converted before the failure; to know more than that, call decode.
    void decode_bytes(const octet_bidirectional_range auto &in, std::vector<std::byte> &out,
                      error_code_ptr ec = nullptr)
    {
        clear_error(ec);
        if (decode(in, std::back_inserter(out)).in != std::ranges::end(in))
            set_error_or_throw<bad_hex_input>(
                ec, std::errc::invalid_argument,
                "not a hexadecimal digit, or an odd number of digits");
    }
} // namespace jio::hex
