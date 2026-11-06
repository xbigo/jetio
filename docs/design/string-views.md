# String views with end guarantees

`std::string_view` says nothing about the memory at its end. The views in `jio/std/` add guarantees about it. They are language extensions, see [project structure](project-structure.md).

| Guarantee | Type | Concept | Meaning |
| --- | --- | --- | --- |
| terminated | `nt_string_view` | `nt_string_view_like` | `data()[size()] == CharT()`; has `c_str()` |
| sentinel | `ns_string_view` | `ns_string_view_like` | scanning from the end reaches `CharT()` without leaving valid memory |
| determinable | `nd_string_view` | `nd_string_view_like` | `data()[size()]` can be read, so "is it `CharT()`?" can be answered |
| none | `std::string_view` | | |

Each guarantee is stronger than all that follow: terminated implies sentinel implies determinable implies a plain string view. The converse does not hold. All three are implemented, in `jio/std/string_view.hpp`, together with the concept `string_view_family`, which is any of the three. A view is in the family because its base `string_view_ops` (a detail class) carries the marker `string_view_family_tag`; the concept looks for the marker, which is safe for incomplete and unrelated types.

Characters before the end may be `CharT()` too, so `strlen(c_str())` can be smaller than `size()`.

The types are templates over the character type, but this project uses only `char`, UTF-8 included: strings inside the project are UTF-8 in `char`, and another encoding is converted only at the boundary of an integration. The shortcuts for the other character types (`nt_wstring_view`, `nt_u8string_view`, ...) are at the end of the header for whoever needs them; the project does not use them.

## Conversions

- Towards a weaker guarantee: static, implicit, constrained by the concepts.
- Towards a stronger one: only to terminated can the program decide, from an ns or nd view, by reading the end (O(1), safe because the end can be read): `view.is_terminated()` and `as_nt(view)`, which returns an `std::optional<nt_string_view>`. Every other direction is a constructor with the tag `assume` and a precondition that cannot be checked (the null check is the exception, and for nt the terminator, see below).

## Guarantee of a type

`end_guarantee_of<T>` is an ordered enum, `none < determinable < sentinel < terminated`. The default is the static member `guarantee` of the type (the three views have it), else `terminated` for a type with a `c_str()` (or an ADL `get_c_str`) that converts to `std::basic_string_view`; specialize it to state the guarantee of another type or to withdraw it. The concepts are the conversion to `std::basic_string_view` plus the guarantee, and each names the weaker one, so overloads pick the most specific.

`get_c_str(x)` is a customization point object: member `c_str()`, or `get_c_str(x)` found by ADL. The size always comes from the conversion to a string view; `data()` is not trusted to be terminated. For sizes in general, `std::ranges::size` is the standard extension point (`std::ranges::data` has none).

## Building an `nt_string_view`

The invariant is established when the view is built and not checked again ("strict in"). A pointer that reaches the library as a string is never trusted without an explicit source capacity (elements that may be read from the pointer), and there is no implicit conversion from a pointer. An array converts to `nt_string_view` only when it is a constant expression (a string literal): the constructor is `consteval`, so a buffer is refused at compile time.

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

## Getting an `ns_string_view` or an `nd_string_view`

All three views have the same two pointer constructors, so that they can be designed and used as a whole; they differ in what they ask of the end of the buffer. `scan_`, the bounded scan for the terminator, is in the base.

| From | Precondition | Level |
| --- | --- | --- |
| nothing | | empty view with a static terminator |
| a view that is stronger, or a type that states such a guarantee (`std::string`) | none for a view of ours; `data() != nullptr` for other types | critical |
| `(p, src_capacity(c))` | `p` not null; a `CharT()` within `[p, p + c)`; the view ends at the first one | critical |
| `(p, n, src_capacity(c))` for nd | `p` not null; `n < c` (the end is inside the buffer) | critical |
| `(p, n, src_capacity(c))` for ns | the same, and a `CharT()` within `[p + n, p + c)` | critical for the first two; audit for the terminator, because finding it costs O(c - n) |
| `rm_suffix(view, n)`, `substr(view, pos, n)` | see below | critical |
| `(assume, std::basic_string_view)` | `data() != nullptr`; the caller promises the guarantee (a readable end, or a terminator at or after the end) | critical for the null check |

`nt_string_view` has the constructor `(assume, std::basic_string_view)` too: it checks `data() != nullptr` and `data()[size()] == CharT()` (critical, which reads the end on the caller's word that it can be read).

## The design of the views

- **Immutable.** No member changes a view in place. `rm_prefix` and `substr` return a new view, and every function that returns a view is `[[nodiscard]]`. Only assignment and swap replace the whole value; assignment is needed because `std::ranges::view` requires a movable type.
- **One base, no inheritance between the views.** What the three have in common is a CRTP base, `detail::string_view_ops` in `string_view.hpp`: it holds the pointer and the size, and has `data()`, `size()`, `is_terminated()`, the iterators, `swap`, `empty`, `operator[]`, `front`, `back`, the conversion to `std::basic_string_view`, `==` and `<=>`. Its constructors and its assignment are private, for the derived view only (`friend Derived`), so a reference to the base can neither build nor assign: a stronger view cannot be weakened through it. A view that inherited from a weaker view could. Each view has its own constructors, which decide what is promised about the end (and nt has `c_str()`), and a `shortened_type`, the view that keeps its guarantee when the end moves back (nt gives ns, ns gives ns, nd gives nd).
- **Strict in.** The invariant is established when the view is built and never checked again. Data members have default member initializers and are set in the member initializer list; a constructor checks the invariant after the members are set, but before it reads through a pointer.
- **Primitives and extensions.** The primitives need the private members, have a meaning that no other member covers, or are needed by the standard algorithms. They are orthogonal, and none of them can break the invariant. The extensions are composed from the primitives, for convenience or to follow a convention. Anything else is a free function.

| | Members |
| --- | --- |
| Primitives | the constructors, `data()`, `size()`, `is_terminated()`, `c_str()` (nt only: the terminator, which no other member covers), `begin`/`end` and the const and reverse variants (the standard algorithms and ranges depend on them), `swap` |
| Extensions | `empty` (`size() == 0` at the same cost), `operator[]`, `front` and `back` (style adapters: the iterators cover them, and they could be free functions), the conversion to `std::basic_string_view`, `==`, `<=>`, `std::hash` |
| Free functions | `substr(view, pos)`, `substr(view, pos, n)`, `rm_prefix`, `rm_suffix`, `as_nt`, and in `string_view_io.hpp` `operator<<` and `std::formatter` |

Shortening: `substr(view, pos)` and `rm_prefix(view, n)` keep the type, because the end does not move. `substr(view, pos, n)` and `rm_suffix(view, n)` return `shortened_type`: a terminated view gives a sentinel one. They build the result with `assume` (see above), which for nt looks at the terminator once more.

The views are defined in the order nd, ns, nt, so that `shortened_type` of nt can name ns; there are no forward declarations, and the default template argument is on the definitions. Output is in `jio/std/string_view_io.hpp`: `operator<<` as a non-friend function, and `std::format` support, which is heavy. The view is a `std::ranges::view`, a borrowed range and a contiguous range; the iterators are plain pointers and `data()` is the buffer address.

## Differences from `std::string_view`

- Immutable, and `substr` and `rm_prefix` are free functions, not members, taking the view by const reference: `substr(view, pos)` and `rm_prefix(view, n)` return a new view, and are the same. `rm_prefix` is not called `remove_prefix`, which changes the view in the standard; `[[nodiscard]]` catches a result that is dropped. They build the view through the public constructor, which looks at the terminator once more (a critical check).
- `substr(view, pos, n)` and `rm_suffix` return an ns view for an nt view, since the terminator is lost. `substr` has the precondition `n <= size() - pos`; the standard clips the count to the end, here a count past the end is a violation (use `substr(view, pos)` for the rest).
- No searching or matching (`find` and its relatives, `starts_with`, `ends_with`, `contains`) and no `compare`: they are algorithms, not operations of the type. Use `std::string_view(view)` or an external algorithm.
- No `at` and no throwing `copy`: bounds are preconditions, not exceptions. No `max_size` and no `length`.
- No `operator<<` and no formatter in the class header: they are free functions in `string_view_io.hpp`, so that the view does not depend on the streams. (The standard headers `<iterator>` and `<ranges>` may still include the stream headers on their own, as libstdc++ does.)
- `operator[]` has the range of `std::basic_string_view`, `pos < size()`; whether the view ends at a terminator is asked with `is_terminated()`, and the terminator itself is read with `c_str()` (nt). `operator[]`, `front`, `back`, `rm_prefix` and `substr` check their bounds in release builds (critical).
- A default view points to a static terminator, not to `nullptr`.
- No implicit conversion from a pointer, and from an array only in `nt_string_view` and only at compile time (`consteval`, the end is the first terminator); no constructor from `(pointer, size)`: a pointer string always comes with an explicit `src_capacity`. No implicit conversion from `std::string_view`; the explicit way is the constructor `(assume, string_view)`.
- Pointers are not accepted as strings by comparison; the other operand must convert to a string view and must not be a pointer. A literal compares through its conversion to `nt_string_view`.
- `c_str()` exists.
- Only `char` is used in this project (see the file comment); the aliases for the other character types are there for whoever needs them.

## Comparison and hashing

Comparison looks at the characters only: not at the address, not at the terminator, and characters before it count. It works with any type that converts to a string view, except pointers, which are not compared. Equal views hash equally, and the hash is the one of `std::basic_string_view`.

## Open

Nothing consumes the sentinel guarantee yet: ns exists so that shortening an nt view loses as little as possible, and no operation needs the sentinel (`is_terminated` and `as_nt` need only a readable end). An operation such as extending a view to the terminator after its end would be the first user. The searching and matching algorithms (free functions) are not written.

Related proposals: P3655 (`cstring_view`), P4220 (design goals), P1402, N3921.

## String literals

`literal("text")` and `"text"_nt` (namespace `jio::literals`, inline) give an `nt_string_view` at compile time. In the project a literal that is kept in a variable is written `constexpr auto s = literal("text");`, since `auto s = "text"` would be a pointer. Both end at the first terminator, as the pointer constructor does.
