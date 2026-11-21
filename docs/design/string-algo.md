# String algorithms

`jio/string_algo/` holds the algorithms on text and bytes that the standard library and the [string views](string-views.md) do not have. They come from `string_algo` of nix, reimplemented. `jio/string_algo.hpp` includes all of them.

| Header | Namespace | Content |
| --- | --- | --- |
| `string.hpp` | `jio::str_algo` | `replace` (a character, or a pattern), `ascii_to_upper`, `ascii_to_lower`, `icase_equal`, `icase_less`, `icase_compare` |
| `utf8.hpp` | `jio::utf8` | `encode`, `decode`, `encode_string`, `decode_string`, `put`, `get`, `encoded_length` |
| `hex.hpp` | `jio::hex` | `encode`, `decode`, `encode_string`, `decode_bytes` |
| `uri.hpp` | `jio::uri` | `encode`, `decode`, `form_encode`, `form_decode`, and the `*_string` forms |
| `json.hpp` | `jio::json` | `escape`, `escape_string` |
| `octet.hpp` | `jio` | the concept `octet` (a character or a byte) and `to_octet` |

## Decisions

- Searching is not ported (`find`, `rfind`, `find_if`, `contains_element` of nix): every string and view converts to `std::string_view`, which has `find`, `rfind`, `starts_with` and `contains`. `append` and the replacement by a generator are left out as well (nothing used them), and so is `char_separator`, which `std::views::split` covers.
- The algorithms are of the standard library kind: they read a range and write to an output iterator (`replace_copy`, `encode`, `decode`, `hex::encode`, ...). The functions with `string` in their name (`encode_string`, `decode_string`, `decode_bytes`, `replace`, the case functions, ...) are simplified wrappers for the common case, not general algorithms: each takes the output as a parameter, a `std::basic_string` of char (`char_string`: its traits and allocator are free, `std::pmr::string` too) or a `std::vector<std::byte>`, and appends to it, through a `std::back_inserter`. They do not return a `jio::string`: it is immutable and is made to be read, so the text is built in a `std::string` and a `jio::string` is made from it where one is wanted. No string builder for now; one will be added if a case needs it. `json` is set aside: whether it should produce text for `jio::string` at all is not decided, and it stays as it is. `hex` may become a part of paths, so it is wanted.
- Because of that the headers of `string_algo` use nothing of the private part of `jio::string` (`detail::string_access`, `build_from_pieces`); they need only the concepts of the string views.
- The case functions are for ASCII only, and say so in their names. They look the character up in a table of 256 entries that is made at compile time (the old ones read a table made at run time from `std::isupper`, which depends on the locale), so they are `constexpr` and have no branch per letter.
- Pointers and arrays are not strings here either: the input is a range of octets (`char`, `unsigned char`, `std::byte`), so a `jio::string`, a view, a `std::string` or a vector of bytes will do.

## Errors

Invalid input is an error, not a contract violation (data from outside, see [contracts](contracts.md)). The functions with `string` in their name report it as in `error.hpp`: with the error code if there is one, else thrown. Each part has a type of its own, all derived from `jio::exception`, which is a `std::system_error` that also keeps the `std::source_location` of the line that reported (`where()`), unlike `io_error`, which stays the exception of the devices and is derived from it as well. The types are small: `struct bad_hex_input : exception { using exception::exception; };`, and `set_error_or_throw<bad_hex_input>(ec, std::errc::invalid_argument, "message")` reports.

| Type | Code | When |
| --- | --- | --- |
| `utf8::invalid_utf8_code` | `illegal_byte_sequence` | a byte that cannot start a character or does not continue one, or input that ends inside a character |
| `utf8::unicode_out_range` | `result_out_of_range` | a value that is not a code point (negative, above `0x7fffffff`), a lone UTF-16 surrogate, or a code point that UTF-16 cannot hold |
| `hex::bad_hex_input` | `invalid_argument` | a character that is not a digit, or an odd number of digits (reported by `decode_bytes`; `decode` only returns the position) |
| `uri::invalid_encoded_uri` | `invalid_argument` | a `%` without two hexadecimal digits |

What the exception support of nix had and this has not: the stack trace (it was never implemented), the macros `NIX_THROW` and `NIX_EXCEPTION_TYPE` and the message that was built with a stream (`std::source_location` and a plain message replace them), and a second base for exceptions without an error code (every exception here has one).

The algorithms on ranges of `utf8` and `hex` and the decoders of `uri` do not report: they stop at the element that failed and return the position (`jio::convert_result`, a `std::ranges::in_out_result` whose iterator is `std::ranges::dangling` for a temporary range), so the part before it is converted, and the input was converted if the position is the end. The functions that append to a string or a vector decide from that (`utf8::decode_string` from the status of the character, to say why) and report; if they fail, the string holds what was converted before the failure, which is all they promise: whoever calls them cares for the two outcomes, all converted or an error, and tracks the old size if the string must be cut back. For more, call `encode`, `decode` or `hex::decode`, which return the position.

## UTF-8

The form is the general one of nix, with up to 6 bytes (code points up to `0x7fffffff`), and the algorithm cares for the structure only: it does not check the ranges of Unicode, so surrogates, overlong forms and values above `0x10ffff` pass, and it is not a validator. Truncated input, and a byte that does not continue a character, are errors; nix did not look at the continuation bytes.

The implementation is short because it reads the length from the first byte: the number of leading ones, `std::countl_one`, is the number of bytes, and the value is the remaining bits followed by six bits from every continuation byte. Encoding finds the length from the width of the value in bits (`7` bits for one byte, `5n + 1` for `n`), writes the lead byte as `n` ones and a zero in front of the top bits, and the other bytes as `10` and six bits.

A wide character type decides how its text is read and written: two bytes (`char16_t`, and `wchar_t` where it is 16 bit) are UTF-16 with surrogate pairs, four bytes are code points, so `decode_string<wchar_t>` and `encode_string` work on every platform. This is the conversion at the boundary of the operating system.

## Other decisions

- `hex::decode` takes both cases and writes `std::byte`; `decode_bytes` returns them in a vector. The digits are decoded with one table of 256 bytes, made at compile time (a lookup does not branch on the kind of digit, and a value above 15 marks a character that is not a digit; `digit_value`, which `uri` uses too, reads the same table). `decode` does not report: it needs a bidirectional range, drops the last digit first if their number is odd, converts pair by pair and stops at the first pair that is not two digits, returning the position; so the loop never looks at the end inside a pair. The input was converted if the position is the end, which is what `decode_bytes` checks before it reports. nix had three tables to pick the case, written out, and a decoding that sized its result at twice the input.
- `uri::encode` keeps the unreserved characters of RFC 3986 and writes the others as `%XX` with capital digits (what to do with a byte is looked up in a table of 256 entries made at compile time, one for each variant); the form variant keeps `!*'()` too, as the original did, and writes a space as `+`. `uri::decode` does not look at what the bytes are, and it does not report, like `hex::decode`: it stops at the first `%` that is not followed by two digits and returns the position, and `decode_string` checks it against the end before it reports.
- `json::escape` writes `\u00xx` for the control characters below `0x20`, a backslash before `"` and `\`, and keeps the rest, bytes of UTF-8 included; `escape_string` puts quotes around.
- `replace` with a pattern searches from the left and does not search what it put in; the pattern must not be empty (the original looped forever).

## Tests

`tests/unit/string_algo/`: for each header the results that show the algorithm works, the lengths from 1 to 6 bytes, UTF-16, and the errors with their position; the error types and the place they report are in `tests/unit/error.cpp`.

## Style of the signatures

- A function template whose parameters are constrained by concepts is written with them in the parameter list (`const octet_forward_range auto &in`, `std::output_iterator<char> auto out`), so the prototype reads as one declaration. The form with `template <...>` stays where the return type needs the name of a type parameter and cannot be written without it (`convert_result<Range, Out>`, `string_of<Separator>`), and where the template argument is given by the caller (`decode_string<char16_t>`, `set_error_or_throw<Exception>`).
- The return type is on the line of the function name, and as much of the parameter list as fits goes on that line too (`PenaltyReturnTypeOnItsOwnLine` in `.clang-format`). A long return type or a long default argument is shortened with a name instead of a line break: `jio::default_resource()`, `jio::string_of<T>`, `jio::convert_result<Range, Out>`.

## Where the functions write

They append to a container that the caller gives, so one buffer can take many conversions and the text is not copied again, and they convert one element at a time without a pass over the input to add up the size first. `utf8::decode_string` takes a `std::basic_string` of `char16_t`, `char32_t` or `wchar_t` (the character type is deduced from it), `utf8::encode_string` and the others a `char_string`. `utf8::put` writes one code point and advances the output iterator it is given by reference.
