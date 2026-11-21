# String

`jio::string` (`jio/std/string.hpp`) is an owning, immutable string that is cheap to copy. It is a language extension, see [project structure](project-structure.md), and does not depend on the io part. Its first user is the path type, which is a string with algorithms of its own. The idea is that of `nix::string`; the code is a new one. Output (`<<`, `std::format`) is in `string_io.hpp`.

## Layouts

An object is `string_object_size` bytes, a constant that is 16 and the same on every platform (little endian only, checked). 16 is the lower bound: the value must be a multiple of 8 and may be raised, 24 gives 23 and 32 gives 31 tiny characters of `char`, and the layout adapts (padding in front of the pointer keeps the tag in the last byte). The unit tests pass for 16, 24 and 32. An object has one of three layouts; the last byte tells which.

| Layout | Characters | Copy | Destroy |
| --- | --- | --- | --- |
| tiny | in the object, up to `string_object_size - 1` for `char` | copies the bytes | nothing |
| shared | in a block `[count][memory_resource*][characters, terminator]` | counts up | counts down, the last frees through the resource of the block |
| borrowed | somewhere else, kept alive by someone else | copies the bytes | nothing |

- Tiny: the last element of the 16 bytes is the room left, `capacity - size`, so a string of full size has `CharT()` there, which is its terminator (the trick of folly). The tag of the other two layouts is 0x80 or more, above any room left.
- Shared and borrowed: the pointer and, in the top byte of the second word, the tag; the size is the 56 bits below it. `max_size()` is that, or less where memory is smaller: a block must be measurable by a `std::ptrdiff_t`, which matters where it is 32 bit.
- A borrowed string is made from literals (`"text"_str`) and from storage that someone else keeps alive, a pool for example: `string(borrow, nt_string_view)`, whose precondition is that the characters outlive the string and its copies. There is no tag for pooled strings apart from the literals: both mean the same to the string. A string made from the characters of a borrowed one is independent of the pool.
- The characters may contain `CharT()` too. A string always ends with a terminator, so `c_str()` is `data()` and it converts implicitly to `nt_string_view` (and to `std::string_view`).
- No hash is stored. Code that needs a cache of hashes builds it without touching the string. `std::hash<string>` is the one of the `std::basic_string_view` with the same characters, so equal strings of any layout and the string views hash alike.
- `data()` and `size()` branch on the layout. Performance of the string is less important than cooperation with the rest of the project, and the layout has no pointer into itself, so a string can be moved as bytes.
- The reference count: increment relaxed, decrement acquire-release. Copies of one string may be made and destroyed from several threads; assignment to one object from several threads is, as always, not safe.
- The literals of a library that is unloaded are not handled: a borrowed string must not outlive the storage it borrows, the unloaded library's included.
- A moved from string is valid and empty.

## Memory

Storage of a shared string comes from a `std::pmr::memory_resource` (never null) whose pointer is kept in the block, so a pool can serve strings that stay reference counted, and the string is freed where it was allocated. There is no allocator parameter on the type: `std::pmr::string` and `jio::string` stay one type each. The resource must outlive every string that uses it. The default is `std::pmr::get_default_resource()`, spelled `jio::default_resource()`; `jio::string_of<T>` is the string of the characters of `T`, for the return types.

A function that makes a string takes the resource as its last parameter, with that default, except `concat_in`, whose parameter pack comes last, so the resource is first.

## Making a string

| From | Result | Notes |
| --- | --- | --- |
| nothing | empty, tiny | `constexpr` |
| `(borrow, nt_string_view)` | borrowed | `constexpr`; the caller promises the lifetime |
| `"text"_str` | borrowed | `consteval`; may contain `\0`; the suffix differs from the standard's `s` |
| `const CharT *` (explicit), or `= pointer` | a copy of the C string up to its terminator | `@pre` not null (critical) and terminated; an array is taken as one; the assignment uses the default resource; `nullptr` is deleted |
| anything a string view can be made from, but a pointer or an array: a string view, `std::span<const char>`, `std::vector<char>`, ... | a copy (explicit) | tiny if it fits, else shared |
| `concat(a, b, ...)`, `concat_in(resource, a, b, ...)` | a copy of the pieces, one allocation | the pieces are string-view-like, not pointers or arrays |
| `join(range, separator, resource)` | the pieces with the separator between | the range is traversed twice |
| `a + b + c` | a `concator`, which converts to a string | see below |

A pointer enters only as a C string, explicitly, and the assignment is the same copy; there is no implicit conversion, so `string s = "text";` does not compile, and the borrowed `"text"_str` is the way to avoid the copy. Where the pieces are ranges of characters (the copy from a string view, `concat`, `join`, `+`), no pointer or array is a source: a range says how many characters there are, a pointer or an array does not say whether the terminator counts. The concept is `string_view_source`. The pieces of `concat` and `join` and the operands of `+` need no guarantee about their end (only the characters are read), so a `std::string_view` or a `std::span<const char>` is accepted; a range of `std::byte` is not.

`concat_in`, `join` and the `concator` copy their pieces with one shared function, `build_from_pieces`, which is given a way to enumerate the pieces and calls it twice, to add up the sizes (checked for overflow) and to copy. It reaches the string through one private function, `build_(resource, n, fill)`, which chooses tiny or shared from `n`, has `fill` write the `n` characters to the final place, checks that exactly `n` were written, and adds the terminator. `basic_string` knows none of the functions that use it: its only friend is the helper `detail::string_access` that wraps `build_`, and `concator` converts to the string, the string does not construct from it. `n` is never given by the caller of a public function: the function derives it from the pieces, with a check for overflow, and the pieces and the copy are the same list. A size over `max_size()` is `std::length_error` (it comes from data, so it is an error, not a contract violation, see [contracts](contracts.md)).

## `a + b`

`operator+` exists when both operands are string-view-like (or a `concator`), have the same character type, and one is a string, a string view of ours or a `concator`. Its result is a `concator`, which keeps references to the operands and the total size. The conversion to a string is rvalue qualified: `string s = a + b + c;` allocates once and copies each piece once, `auto x = a + b; string s = x;` does not compile. A `concator` must not be kept, since it refers to its operands; the rule is a convention that the qualified conversion backs up. `concator::to_string(resource)` is the named conversion with another resource.

## Left out on purpose

- Hash cache, substrings that share the characters of the original (one small substring would keep a large block alive; a substring is a view), mutation (a string is replaced as a whole), an allocator on the type, a tag for pooled strings.
- Reading the string in a constant expression. Constructing one in a constant expression is supported for the empty and borrowed layouts, so that global strings need no dynamic initialization.

## Measured: `a + b + c`

`tests/bench/string.cpp` makes a string of three pieces (each a third of the length) in several ways, and counts the allocations of one iteration (the replaced `operator new` of the benchmark counts them, those of `jio::string` too). Median of 3 runs of 0.1 s, in the release presets run natively (the container was not available), on the AMD Ryzen 7 7840H of the [build matrix](build-matrix.md); times are CPU ns, then allocations. All are `-O3 -DNDEBUG -g -std=c++23`, without `-march` and without LTO; contract checks are at the level of a release build (critical ones only).

| Result | 12 chars | 18 chars | 48 chars | 192 chars |
| --- | --- | --- | --- | --- |
| **GCC 16, libstdc++** (tiny up to 15 chars in both strings) | | | | |
| the concator `a + b + c` of `jio::string` | 9.8 / 0 | 21.9 / 1 | 20.6 / 1 | 19.6 / 1 |
| `concat(a, b, c)` | 9.9 / 0 | 20.5 / 1 | 20.7 / 1 | 19.1 / 1 |
| `std::string` `+=` twice, then `jio::string(t)` | 9.8 / 0 | 30.6 / 2 | 42.7 / 4 | 39.9 / 4 |
| the same with `reserve` | 11.9 / 0 | 29.5 / 2 | 27.7 / 2 | 27.1 / 2 |
| `std::string` `a + b + c`, no `jio::string` | 12.9 / 0 | 15.2 / 1 | 20.6 / 2 | 18.9 / 2 |
| **Clang 21, libstdc++** (the same library as above) | | | | |
| the concator `a + b + c` of `jio::string` | 13.5 / 0 | 23.6 / 1 | 24.3 / 1 | 22.7 / 1 |
| `concat(a, b, c)` | 11.4 / 0 | 21.1 / 1 | 22.2 / 1 | 20.5 / 1 |
| `std::string` `+=` twice, then `jio::string(t)` | 15.3 / 0 | 35.6 / 2 | 49.8 / 4 | 48.9 / 4 |
| the same with `reserve` | 15.7 / 0 | 29.6 / 2 | 29.5 / 2 | 29.1 / 2 |
| `std::string` `a + b + c`, no `jio::string` | 13.4 / 0 | 21.9 / 1 | 26.7 / 2 | 25.2 / 2 |
| **Clang 22, libc++ 22** (tiny up to 15 chars for `jio::string`, 22 for `std::string`) | | | | |
| the concator `a + b + c` of `jio::string` | 12.4 / 0 | 22.7 / 1 | 22.7 / 1 | 21.7 / 1 |
| `concat(a, b, c)` | 10.9 / 0 | 19.5 / 1 | 20.1 / 1 | 19.2 / 1 |
| `std::string` `+=` twice, then `jio::string(t)` | 24.7 / 0 | 33.6 / 1 | 42.7 / 3 | 48.5 / 4 |
| the same with `reserve` | 28.1 / 0 | 35.0 / 1 | 33.3 / 2 | 34.0 / 2 |
| `std::string` `a + b + c`, no `jio::string` | 15.1 / 0 | 17.0 / 0 | 23.0 / 2 | 22.3 / 2 |

The parts, for strings of the same total length (the empty strings have no length):

| Part | 12 chars | 18 chars | 48 chars | 192 chars |
| --- | --- | --- | --- | --- |
| **GCC 16, libstdc++** | | | | |
| the loop alone | 0.1 / 0 | 0.1 / 0 | 0.1 / 0 | 0.1 / 0 |
| copy of a `jio::string` | 0.6 / 0 | 2.6 / 0 | 2.5 / 0 | 2.5 / 0 |
| copy of a `std::string` | 2.5 / 0 | 6.9 / 1 | 6.7 / 1 | 7.2 / 1 |
| `jio::string` from a `std::string_view` | 2.1 / 0 | 14.5 / 1 | 14.3 / 1 | 15.0 / 1 |
| `operator new` and `operator delete` | 4.6 / 1 | 4.6 / 1 | 4.6 / 1 | 4.6 / 1 |
| `allocate` and `deallocate` of the default resource | 10.0 / 1 | 10.0 / 1 | 10.1 / 1 | 10.0 / 1 |
| an empty `jio::string`, an empty `std::string` | 0.4, 0.6 | | | |
| **Clang 22, libc++ 22** | | | | |
| the loop alone | 0.2 / 0 | 0.2 / 0 | 0.2 / 0 | 0.2 / 0 |
| copy of a `jio::string` | 0.8 / 0 | 2.5 / 0 | 2.5 / 0 | 2.5 / 0 |
| copy of a `std::string` | 0.6 / 0 | 0.6 / 0 | 7.1 / 1 | 7.4 / 1 |
| `jio::string` from a `std::string_view` | 8.3 / 0 | 14.7 / 1 | 14.2 / 1 | 19.3 / 1 |
| `operator new` and `operator delete` | 4.5 / 1 | 4.6 / 1 | 4.6 / 1 | 4.4 / 1 |
| `allocate` and `deallocate` of the default resource | 9.2 / 1 | 8.9 / 1 | 8.8 / 1 | 9.1 / 1 |
| an empty `jio::string`, an empty `std::string` | 0.4, 0.4 | | | |

- The concator makes one allocation when the result is not tiny, none when it is, which is the least a string that owns its block can do. Without it, a `jio::string` made through a `std::string` takes two to four allocations and is 1.5 to 2 times slower; with `reserve` it is two allocations and about 1.3 times slower. The concator costs the same as `concat` (within 3.5 ns), since it is the same copy.
- A `std::string` `a + b + c` takes two allocations and about as long as one allocation of `jio::string`: the operators reuse the temporary. So the concator is as fast as `std::string` and makes a `jio::string`, which is cheap to copy afterwards, with half the allocations.
- The one place where `jio::string` loses is the length between its limit and the one of the standard library: 18 characters with libc++, where a `std::string` stays in the object and has no allocation (17.0 ns against 22.7 ns). `string_object_size` of 24 would put the limit at 23.
- Where the time goes, from the parts: the loop and an empty string cost nothing (0.1 and 0.4 ns), the allocator alone 4.6 ns, copying a `jio::string` 0.6 ns if it is tiny and 2.6 ns if it has a block (an atomic increment and decrement), and the copy of the bytes is cheap (from 48 to 192 characters the result grows by about 1 to 4 ns). The memory resource costs most of what a `jio::string` adds: the same `allocate` and `deallocate` through the default resource of `std::pmr` take 10.0 ns, 5.4 ns more than `operator new` and `operator delete` (one virtual call for each). A `jio::string` made of a view takes about 14 ns with a block, the resource included, against 7 ns for copying a `std::string`. Skipping the atomic decrement when the block has one reference was tried: the times did not change by more than the noise (0.5 ns), and it was not kept. A tiny `jio::string` of a view takes 2.1 ns with GCC and 8.3 ns with Clang, which is most of what Clang loses on the concator at 12 characters; why was not looked into.
- The toolchains: the code of `jio::string` takes about the same time with both libraries (Clang with libstdc++ and with libc++ differ by under 2 ns on the concator and `concat`), and GCC is faster than Clang on it by 2 to 4 ns, so that part is the compiler. The rows of `std::string` differ with the library, most at 12 characters, where nothing is allocated (`+=` twice: 9.8 ns with GCC and libstdc++, 15.3 with Clang and libstdc++, 24.7 with libc++); how much of the member functions of `std::string` each compiler inlines was not checked.
- Clang with `-O2` instead of `-O3` takes the same time: of the 44 cases with each library, 11 with libstdc++ and 6 with libc++ differ by more than 1 ns, all at 18 or 192 characters, and the largest differences with `-O2` are 1.6 ns (concator, 18 characters, libstdc++) and 1.8 ns (std_string_append, 48 characters, libc++), slower than with `-O3`.

## Tests

`tests/unit/std/string.cpp`: the layouts at the sizes around the limit (relative to `string_object_size`, which the tests do not assume), the tag in the last byte and the size in the bytes below it, read from the bytes of the object so that a wrong placement shows on a 32 bit platform too (not yet built for one), `max_size()`, resource counts (tiny uses none, copies share, one allocation per concatenation), moves, assignments between layouts, borrowed strings, the literal, comparison and hash, `concat`, `concator`, `join`, output, `wstring`, and copies in four threads (checked by the TSan preset).
