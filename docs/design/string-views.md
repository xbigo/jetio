# String views with end guarantees

`std::string_view` says nothing about the memory at its end. The views in `jetio/std/` add guarantees about it. They are language extensions, see [project structure](project-structure.md).

| Guarantee | Type | Concept | Meaning |
| --- | --- | --- | --- |
| terminated | `nt_string_view` | `nt_string_view_like` | `data()[size()] == CharT()`; has `c_str()` |
| sentinel | `ns_string_view` | `ns_string_view_like` | scanning from the end reaches `CharT()` without leaving valid memory |
| determinable | `nd_string_view` | `nd_string_view_like` | `data()[size()]` can be read, so "is it `CharT()`?" can be answered |
| none | `std::string_view` | | |

Each guarantee is stronger than all that follow: terminated implies sentinel implies determinable implies a plain string view. The converse does not hold. Only `nt_string_view` is implemented so far; `ns_string_view` and `nd_string_view` are planned.

Characters before the end may be `CharT()` too, so `strlen(c_str())` can be smaller than `size()`.

The types are templates over the character type, but this project uses only `char`, UTF-8 included: strings inside the project are UTF-8 in `char`, and another encoding is converted only at the boundary of an integration. So there is only the alias `nt_string_view`.

## Conversions

- Towards a weaker guarantee: static, implicit, constrained by the concepts.
- Towards a stronger one: only from determinable or sentinel to terminated can the program decide, by reading `*end()` (O(1), safe because the end is readable). Every other direction is a function with a precondition that cannot be checked.

## Guarantee of a type

`end_guarantee_of<T>` is an ordered enum, `none < determinable < sentinel < terminated`. The default is `terminated` for a type with a `c_str()` (or an ADL `get_c_str`) that converts to `std::basic_string_view`; specialize it to state the guarantee of another type or to withdraw it. The concepts are the conversion to `std::basic_string_view` plus the guarantee, and each names the weaker one, so overloads pick the most specific.

`get_c_str(x)` is a customization point object: member `c_str()`, or `get_c_str(x)` found by ADL. The size always comes from the conversion to a string view; `data()` is not trusted to be terminated. For sizes in general, `std::ranges::size` is the standard extension point (`std::ranges::data` has none).

## Building an `nt_string_view`

The invariant is established when the view is built and not checked again ("strict in"). A pointer that reaches the library as a string is never trusted without an explicit source capacity (elements that may be read from the pointer), and there is no implicit conversion from a pointer or an array.

| From | Precondition | Level |
| --- | --- | --- |
| nothing | | empty view with a static terminator |
| `(p, src_capacity(c))` | `p` not null; a `CharT()` within `[p, p + c)`; the view ends at the first one | critical |
| `(p, n, src_capacity(c))` | `p` not null; `n < c`; `p[n] == CharT()` | critical |
| `nt_string_view` | none: a copy checks nothing | |
| another type with a terminated guarantee | `c_str()[size] == CharT()`, checked because the type only promises it | critical |
| `std::string_view` | not provided | |
| `nullptr` | deleted | |

The explicit lengths and capacities make the use of raw pointers long on purpose: the better way is to pass a `std::string` or a similar object.

## The design of `nt_string_view`

- **Immutable.** No member changes a view in place. `rm_prefix` and `substr` return a new view, and every function that returns a view is `[[nodiscard]]`. Only assignment and swap replace the whole value; assignment is needed because `std::ranges::view` requires a movable type.
- **Strict in.** The invariant is established when the view is built and never checked again. Data members have default member initializers and are set in the member initializer list; a constructor checks the invariant after the members are set, but before it reads through a pointer.
- **Primitives and extensions.** The primitives need the private members, have a meaning that no other member covers, or are needed by the standard algorithms. They are orthogonal, and none of them can break the invariant. The extensions are composed from the primitives, for convenience or to follow a convention. Anything else is a free function.

| | Members |
| --- | --- |
| Primitives | the constructors, `data()`, `size()`, `c_str()` (the terminator, which no other member covers), `begin`/`end` and the const and reverse variants (the standard algorithms and ranges depend on them), `swap` |
| Extensions | `empty` (`size() == 0` at the same cost), `operator[]`, `front` and `back` (style adapters: the iterators cover them, and they could be free functions), the conversion to `std::basic_string_view`, `==`, `<=>`, `std::hash` |
| Free functions | `substr`, `rm_prefix`, and in `nt_string_view_io.hpp` `operator<<` and `std::formatter` |

Forward declarations of the three views are in `jetio/std/string_view_fwd.hpp`, which is the only place for the default template argument. Output is in `jetio/std/nt_string_view_io.hpp`: `operator<<` as a non-friend function, and `std::format` support, which is heavy. The view is a `std::ranges::view`, a borrowed range and a contiguous range; the iterators are plain pointers and `data()` is the buffer address.

## Differences from `std::string_view`

- Immutable, and `substr` and `rm_prefix` are free functions, not members, taking the view by const reference: `substr(view, pos)` and `rm_prefix(view, n)` return a new view, and are the same. `rm_prefix` is not called `remove_prefix`, which changes the view in the standard; `[[nodiscard]]` catches a result that is dropped. They build the view through the public constructor, which looks at the terminator once more (a critical check).
- No `rm_suffix` and no `substr(pos, n)`: they cannot keep the terminator. They are expected to return an `ns_string_view` once it exists.
- No searching or matching (`find` and its relatives, `starts_with`, `ends_with`, `contains`) and no `compare`: they are algorithms, not operations of the type. Use `std::string_view(view)` or an external algorithm.
- No `at` and no throwing `copy`: bounds are preconditions, not exceptions. No `max_size` and no `length`.
- No `operator<<` and no formatter in the class header: they are free functions in `nt_string_view_io.hpp`, so that the view does not depend on the streams. (The standard headers `<iterator>` and `<ranges>` may still include the stream headers on their own, as libstdc++ does.)
- `operator[]` accepts `size()`, which is the terminator. `operator[]`, `front`, `back`, `rm_prefix` and `substr` check their bounds in release builds (critical).
- A default view points to a static terminator, not to `nullptr`.
- No implicit conversion from a pointer or an array, no constructor from `(pointer, size)`: a pointer string always comes with an explicit `src_capacity`. No implicit conversion from `std::string_view`.
- Pointers and arrays are not accepted as strings by comparison; the other operand must convert to a string view and must not be a pointer.
- `c_str()` exists.
- Only `char` is used in this project (see the file comment), so there are no aliases for the other character types.

## Comparison and hashing

Comparison looks at the characters only: not at the address, not at the terminator, and characters before it count. It works with any type that converts to a string view, except pointers and arrays, which are not compared. Equal views hash equally, and the hash is the one of `std::basic_string_view`.

## Open

`rm_suffix` and `substr(pos, n)` will return a sentinel view. Shared code of the three types is a stateless CRTP mixin, not inheritance (a base reference would allow weakening the invariant through assignment or `remove_suffix`).

Related proposals: P3655 (`cstring_view`), P4220 (design goals), P1402, N3921.
