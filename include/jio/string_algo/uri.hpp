#pragma once

// Percent encoding of URIs (RFC 3986) and its form variant, where a space is a plus sign.
//
// encode keeps the unreserved characters (letters, digits and -_.~) and writes every other
// byte as %XX with capital digits. form_encode keeps !*'() too and writes a space as +. The
// decoders write the byte of every %XX, and form_decode also turns + into a space; they do not
// look at what the bytes are.
//
// The decoders on ranges do not report: they convert up to the first % that is not followed by
// two hexadecimal digits, in the middle or at the end, and return the position they stopped at,
// so the input was converted if the position is the end. The functions that append to a string
// decide from the position and report as in error.hpp: with the error code if there is one, else
// thrown as uri::invalid_encoded_uri (invalid argument); if they fail the string holds what was
// converted before the failure, which is all they promise.

#include <jio/error.hpp>
#include <jio/string_algo/hex.hpp>
#include <jio/string_algo/octet.hpp>

#include <array>
#include <cstddef>
#include <iterator>
#include <ranges>
#include <string>
#include <string_view>

namespace jio::uri
{
    struct invalid_encoded_uri : exception
    {
        using exception::exception;
    };

    namespace detail
    {
        enum class kind : unsigned char
        {
            keep,
            space,
            escape
        };

        /// What encoding does with every byte.
        template <bool Form> constexpr std::array<kind, 256> make_kind_table() noexcept
        {
            std::array<kind, 256> table{};
            table.fill(kind::escape);
            for (unsigned char c = 'A'; c <= 'Z'; ++c)
                table[c] = kind::keep;
            for (unsigned char c = 'a'; c <= 'z'; ++c)
                table[c] = kind::keep;
            for (unsigned char c = '0'; c <= '9'; ++c)
                table[c] = kind::keep;
            table['-'] = table['_'] = table['.'] = table['~'] = kind::keep;
            if constexpr (Form)
            {
                table['!'] = table['*'] = table['\''] = table['('] = table[')'] = kind::keep;
                table[' '] = kind::space;
            }
            return table;
        }

        template <bool Form>
        inline constexpr std::array<kind, 256> kind_table = make_kind_table<Form>();

        template <bool Form> constexpr kind classify(unsigned char c) noexcept
        {
            return kind_table<Form>[c];
        }

        template <bool Form>
        constexpr auto encode_with(octet_range auto &&in, std::output_iterator<char> auto out)
        {
            constexpr std::string_view digits = "0123456789ABCDEF";
            for (const auto &byte : in)
            {
                const unsigned char c = to_octet(byte);
                switch (classify<Form>(c))
                {
                case kind::keep:
                    *out++ = static_cast<char>(c);
                    break;
                case kind::space:
                    *out++ = '+';
                    break;
                case kind::escape:
                    *out++ = '%';
                    *out++ = digits[c >> 4];
                    *out++ = digits[c & 0xf];
                    break;
                }
            }
            return out;
        }

        template <bool Form, octet_forward_range Range, std::output_iterator<char> Out>
        convert_result<Range, Out> decode_with(Range &&in, Out out)
        {
            auto first = std::ranges::begin(in);
            const auto last = std::ranges::end(in);
            while (first != last)
            {
                auto next = first;
                // the value of the next digit, or -1 if it is not one or the input has ended
                const auto digit = [&]
                { return next == last ? -1 : hex::digit_value(to_octet(*next++)); };

                int c = to_octet(*next++);
                if (c == '%')
                {
                    const int high = digit();
                    const int low = digit();
                    if ((high | low) < 0)
                        break;
                    c = (high << 4) | low;
                }
                else if (Form && c == '+')
                {
                    c = ' ';
                }
                *out++ = static_cast<char>(c);
                first = next;
            }
            return {std::move(first), std::move(out)};
        }

        /// Appends the decoded bytes of the text to `out`, as chars; reports a failure.
        template <bool Form>
        void decode_to_string(const octet_forward_range auto &in, char_string auto &out,
                              error_code_ptr ec)
        {
            clear_error(ec);
            if (decode_with<Form>(in, std::back_inserter(out)).in != std::ranges::end(in))
                set_error_or_throw<invalid_encoded_uri>(ec, std::errc::invalid_argument,
                                                        "% without two hexadecimal digits");
        }
    } // namespace detail

    /// The text of the bytes, as above.
    /// @return the output iterator after it
    constexpr auto encode(octet_range auto &&in, std::output_iterator<char> auto out)
    {
        return detail::encode_with<false>(in, std::move(out));
    }
    constexpr auto form_encode(octet_range auto &&in, std::output_iterator<char> auto out)
    {
        return detail::encode_with<true>(in, std::move(out));
    }

    /// The bytes of the text, written as char, up to the first % that is not followed by two
    /// digits.
    /// @return where the input stopped, which is the end only if all of it was converted, and the
    ///         output iterator after the bytes written
    auto decode(octet_forward_range auto &&in, std::output_iterator<char> auto out)
    {
        return detail::decode_with<false>(std::forward<decltype(in)>(in), std::move(out));
    }
    auto form_decode(octet_forward_range auto &&in, std::output_iterator<char> auto out)
    {
        return detail::decode_with<true>(std::forward<decltype(in)>(in), std::move(out));
    }

    /// Appends the text of the bytes to `out`.
    void encode_string(const octet_range auto &in, char_string auto &out)
    {
        encode(in, std::back_inserter(out));
    }
    void form_encode_string(const octet_range auto &in, char_string auto &out)
    {
        form_encode(in, std::back_inserter(out));
    }

    /// Appends the decoded bytes to `out`, as chars: what they mean is not looked at. If it fails,
    /// `out` is valid and holds what was converted before the failure; to know more than that,
    /// call decode.
    void decode_string(const octet_forward_range auto &in, char_string auto &out,
                       error_code_ptr ec = nullptr)
    {
        detail::decode_to_string<false>(in, out, ec);
    }
    void form_decode_string(const octet_forward_range auto &in, char_string auto &out,
                            error_code_ptr ec = nullptr)
    {
        detail::decode_to_string<true>(in, out, ec);
    }
} // namespace jio::uri
