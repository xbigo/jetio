#pragma once

// Conversion between code points and UTF-8.
//
// The form is the general one of the original design, with up to 6 bytes per code point (up to
// 0x7fffffff), and the algorithm cares for the structure only: it does not check that a code
// point is in the ranges of Unicode (no surrogates, no overlong forms, no limit at 0x10ffff),
// so it is not a validator. Truncated input and a byte that is not a continuation byte are
// errors.
//
// Elements that are not UTF-8 are code points, or UTF-16 units where an element has two bytes
// (char16_t, and wchar_t where it is 16 bit): a character type decides how its string is read
// and written, so wide strings work on every platform.
//
// The algorithms on ranges (decode, encode) do not report: they convert up to the first element
// that fails and return the position they stopped at, so the input was converted if the position
// is the end. The functions that append to a std::basic_string (encode_string, decode_string)
// decide and report as in error.hpp, with the error code if there is one, else thrown as
// utf8::invalid_utf8_code (illegal byte sequence) or utf8::unicode_out_range (a value that is
// not a code point or cannot be written in the target); if they fail the string holds what was
// converted before the failure, which is all they promise.

#include <jio/error.hpp>
#include <jio/string_algo/octet.hpp>

#include <algorithm>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <ranges>
#include <string>
#include <type_traits>

namespace jio::utf8
{
    struct unicode_out_range : exception
    {
        using exception::exception;
    };
    struct invalid_utf8_code : exception
    {
        using exception::exception;
    };

    inline constexpr std::uint32_t max_code_point = 0x7fffffffu;

    enum class decode_status : unsigned char
    {
        ok,
        invalid,   ///< a byte that cannot start a character, or that is not a continuation byte
        truncated, ///< the input ends inside a character
    };

    template <typename Iterator> struct decoded
    {
        std::uint32_t code_point;
        Iterator next; ///< after the character, or where it began if the status is not ok
        decode_status status;
    };

    /// @pre cp <= max_code_point
    /// @return the number of bytes of the encoding of cp, 1 to 6
    [[nodiscard]] constexpr std::size_t encoded_length(std::uint32_t cp) noexcept
    {
        JIO_Expects(cp <= max_code_point);
        // n bytes hold 7 bits if n is 1, else 5n + 1
        const int bits = std::bit_width(cp);
        return bits <= 7 ? 1 : static_cast<std::size_t>((bits + 3) / 5);
    }

    /// Writes the encoding of one code point, and advances `out` past it.
    /// @pre cp <= max_code_point
    constexpr void put(std::uint32_t cp, std::output_iterator<char> auto &out)
    {
        const auto n = static_cast<unsigned>(encoded_length(cp));
        if (n == 1)
        {
            *out++ = static_cast<char>(cp);
            return;
        }
        // n ones and a zero, then the top bits; every other byte is 10 and six bits
        *out++ = static_cast<char>(((0xff00u >> n) & 0xffu) | (cp >> (6 * (n - 1))));
        for (unsigned shift = 6 * (n - 1); shift != 0;)
        {
            shift -= 6;
            *out++ = static_cast<char>(0x80u | ((cp >> shift) & 0x3fu));
        }
    }

    /// Reads one code point.
    /// @pre first != last
    template <std::forward_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
        requires octet<std::iter_value_t<Iterator>>
    [[nodiscard]] constexpr decoded<Iterator> get(Iterator first, Sentinel last)
    {
        const unsigned char lead = to_octet(*first);
        Iterator next = first;
        ++next;
        if (lead < 0x80)
            return {lead, next, decode_status::ok};

        // the number of ones in front is the number of bytes: 10xxxxxx is a continuation byte,
        // and 1111111x leads nothing
        const int n = std::countl_one(lead);
        if (n == 1 || n > 6)
            return {0, first, decode_status::invalid};

        std::uint32_t value = lead & (0xffu >> (n + 1));
        for (int i = 1; i < n; ++i, ++next)
        {
            if (next == last)
                return {0, first, decode_status::truncated};
            const unsigned char c = to_octet(*next);
            if ((c & 0xc0) != 0x80)
                return {0, first, decode_status::invalid};
            value = (value << 6) | (c & 0x3f);
        }
        return {value, next, decode_status::ok};
    }

    namespace detail
    {
        /// A range of code points, or of UTF-16 units where an element has two bytes.
        template <typename R>
        concept wide_range =
            std::ranges::forward_range<R> && std::integral<std::ranges::range_value_t<R>> &&
            sizeof(std::ranges::range_value_t<R>) > 1;

        /// A character type that holds code points or UTF-16 units.
        template <typename T>
        concept wide_character = character<T> && sizeof(T) > 1;

        inline void report_decode(error_code_ptr ec, decode_status status)
        {
            set_error_or_throw<invalid_utf8_code>(ec, std::errc::illegal_byte_sequence,
                                                  status == decode_status::truncated
                                                      ? "truncated UTF-8 sequence"
                                                      : "invalid UTF-8 sequence");
        }

        inline void report_range(error_code_ptr ec)
        {
            set_error_or_throw<unicode_out_range>(ec, std::errc::result_out_of_range,
                                                  "not a code point of the target");
        }

        template <typename Iterator> struct wide_code_point
        {
            std::uint32_t code_point;
            Iterator next;
            bool ok;
        };

        /// One code point of a range of wide characters: the element itself if it has four
        /// bytes or more, else UTF-16, where a surrogate pair is one code point.
        template <std::forward_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
        constexpr wide_code_point<Iterator> next_wide(Iterator first, Sentinel last)
        {
            using unsigned_type = std::make_unsigned_t<std::iter_value_t<Iterator>>;
            const auto unit = static_cast<std::uint64_t>(static_cast<unsigned_type>(*first));
            Iterator next = first;
            ++next;
            if constexpr (sizeof(unsigned_type) > 2)
            {
                return {static_cast<std::uint32_t>(unit), next, unit <= max_code_point};
            }
            else
            {
                if (unit < 0xd800 || unit > 0xdfff)
                    return {static_cast<std::uint32_t>(unit), next, true};
                if (unit >= 0xdc00 || next == last) // a low surrogate, or a high one alone
                    return {0, first, false};
                const auto low = static_cast<std::uint64_t>(static_cast<unsigned_type>(*next));
                if (low < 0xdc00 || low > 0xdfff)
                    return {0, first, false};
                ++next;
                const auto cp = 0x10000u + (static_cast<std::uint32_t>(unit - 0xd800) << 10) +
                                static_cast<std::uint32_t>(low - 0xdc00);
                return {cp, next, true};
            }
        }

        /// Whether CharT can hold cp: UTF-16 stops at 0x10ffff, the wider types take every value.
        template <typename CharT> constexpr bool can_hold(std::uint32_t cp) noexcept
        {
            return sizeof(CharT) > 2 || cp <= 0x10ffff;
        }

        /// Writes cp as one or two elements of CharT, and advances `out` past them.
        /// @pre can_hold<CharT>(cp)
        template <typename CharT>
        constexpr void put_wide(std::uint32_t cp, std::output_iterator<CharT> auto &out)
        {
            if (sizeof(CharT) > 2 || cp < 0x10000)
            {
                *out++ = static_cast<CharT>(cp);
            }
            else
            {
                cp -= 0x10000;
                *out++ = static_cast<CharT>(0xd800 + (cp >> 10));
                *out++ = static_cast<CharT>(0xdc00 + (cp & 0x3ff));
            }
        }
    } // namespace detail

    /// UTF-8 to code points, up to the first character that fails.
    /// @return where the input stopped, which is the end only if all of it was converted, and
    ///         the output iterator after the code points written
    template <octet_forward_range Range, std::output_iterator<std::uint32_t> Out>
    convert_result<Range, Out> decode(Range &&in, Out out)
    {
        auto first = std::ranges::begin(in);
        const auto last = std::ranges::end(in);
        while (first != last)
        {
            const auto c = get(first, last);
            if (c.status != decode_status::ok)
                break;
            *out++ = c.code_point;
            first = c.next;
        }
        return {std::move(first), std::move(out)};
    }

    /// Code points to UTF-8, up to the first element that fails; the elements are UTF-16 units
    /// if they have two bytes.
    /// @return where the input stopped, which is the end only if all of it was converted, and
    ///         the output iterator after the bytes written
    template <detail::wide_range Range, std::output_iterator<char> Out>
    convert_result<Range, Out> encode(Range &&in, Out out)
    {
        auto first = std::ranges::begin(in);
        const auto last = std::ranges::end(in);
        while (first != last)
        {
            const auto c = detail::next_wide(first, last);
            if (!c.ok)
                break;
            put(c.code_point, out);
            first = c.next;
        }
        return {std::move(first), std::move(out)};
    }

    /// Appends the UTF-8 text of code points, or of UTF-16 units if they have two bytes, to `out`.
    /// If it fails, `out` is valid and holds what was converted before the failure; to know more
    /// than that, call encode.
    void encode_string(const detail::wide_range auto &in, char_string auto &out,
                       error_code_ptr ec = nullptr)
    {
        clear_error(ec);
        if (encode(in, std::back_inserter(out)).in != std::ranges::end(in))
            detail::report_range(ec);
    }

    /// Appends the code points of UTF-8 text to `out`, as UTF-16 units if its character type has
    /// two bytes, else as code points. If it fails, `out` is valid and holds what was converted
    /// before the failure; to know more than that, call decode.
    template <detail::wide_character CharT, typename Traits, typename Allocator>
    void decode_string(const octet_forward_range auto &in,
                       std::basic_string<CharT, Traits, Allocator> &out,
                       error_code_ptr ec = nullptr)
    {
        clear_error(ec);
        auto sink = std::back_inserter(out);
        const auto last = std::ranges::end(in);
        for (auto first = std::ranges::begin(in); first != last;)
        {
            const auto c = get(first, last);
            if (c.status != decode_status::ok)
            {
                detail::report_decode(ec, c.status);
                return;
            }
            if (!detail::can_hold<CharT>(c.code_point))
            {
                detail::report_range(ec);
                return;
            }
            detail::put_wide<CharT>(c.code_point, sink);
            first = c.next;
        }
    }
} // namespace jio::utf8
