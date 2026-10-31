# String views with end guarantees

`std::string_view` says nothing about the memory at its end. The views in `jetio/std/` add guarantees about it. They are language extensions, see [project structure](project-structure.md).

| Guarantee | Type | Concept | Meaning |
| --- | --- | --- | --- |
| terminated | `nt_string_view` | `nt_string_view_like` | `data()[size()] == CharT()`; has `c_str()` |
| sentinel | `ns_string_view` | `ns_string_view_like` | scanning from the end reaches `CharT()` without leaving valid memory |
| determinable | `nd_string_view` | `nd_string_view_like` | `data()[size()]` can be read, so "is it `CharT()`?" can be answered |
| none | `std::string_view` | | |

Each guarantee is stronger than all that follow: terminated implies sentinel implies determinable implies a plain string view. The converse does not hold. Only `nt_string_view` is implemented so far; `ns_string_view` and `nd_string_view` are planned.

Characters before the end may be `CharT()` too, so `strlen(c_str())` can be smaller than `size()`. The types are templates over the character type (`nt_wstring_view`, `nt_u8string_view`, ...).

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

## Comparison and hashing

Comparison looks at the characters only: not at the address, not at the terminator, and characters before it count. It works with any type that converts to a string view, except pointers and arrays, which are not compared. Equal views hash equally, and the hash is the one of `std::basic_string_view`.

## Open

How `remove_prefix`, `remove_suffix` and `substr` look: `remove_suffix` and `substr(pos, n)` cannot keep the terminated guarantee, they are expected to give a sentinel view. Shared code of the three types is a stateless CRTP mixin, not inheritance (a base reference would allow weakening the invariant through assignment or `remove_suffix`).

Related proposals: P3655 (`cstring_view`), P4220 (design goals), P1402, N3921.
