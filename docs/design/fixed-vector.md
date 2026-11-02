# fixed_vector

`jetio/std/fixed_vector.hpp`. A vector over memory it does not allocate: the elements `[0, size)` of a buffer with room for `capacity` elements. It has the interface of `std::vector` except growth, and takes care of constructing and destroying the elements, which is the hard part of working in raw memory.

## Represent

The buffer is described by a represent, a triple:

| Part | Type |
| --- | --- |
| `ptr` | `T *` |
| `size` | `std::size_t` |
| `capacity` | `std::size_t` |

A represent has these as data members of that name, or as the accessors `ptr()`, `size()` and `capacity()`. `buffer_represent<T>` is the plain triple. The concept `fixed_represent<Rep, T>` states it; the vector only reads it.

The vector reads the triple once, in its constructor, and works on a copy of its own, so its operations never go through the represent. It writes nothing back. Whoever uses the vector does that, with the size it reports or that `release()` returns, as `resize` does for its helper: the helper vector only reports what it made, and `resize` decides what the size becomes. A represent that binds a reference to the size has no live view of the size; that was given up for a definite state of the vector, and nothing needs it.

The constructor takes over the elements in the buffer (`size` of them are constructed already, and the size is not necessarily 0). The represent is the meaning of the buffer, not a physical layout: a buffer, a counter that the program keeps, or a header in the memory can all say these three things.

## Ownership and exception safety

- The vector owns the elements, not the memory. The destructor destroys the elements. `v.release()` gives them up: it returns a plain `buffer_represent` from which the caller destroys the elements and learns how many there are, and the vector is left empty and with capacity 0, so it is also full and cannot overwrite what it gave up. No flag is needed: an empty vector destroys nothing.
- A function that builds elements in a buffer of its caller needs no `try` and `catch`: the vector destroys what it built when an exception leaves the function, and `release()` hands the elements over when it succeeds.
- `push_back` and `emplace_back` construct first and count the element afterwards, so a constructor that throws leaves the vector as it was. Everything built on `emplace_back` gets its exception safety from that: `resize` adds elements, made of its arguments (none for `resize(n)`, the value for `resize(n, value)`), to a temporary vector over the free part of the buffer, which destroys them again if one cannot be made and gives them up when all are there. `erase` gives the basic guarantee (the elements stay valid).
- Moving transfers the elements and the triple; the moved-from vector is empty and has no capacity. Copying is not provided (the memory is not the vector's).

## Interface

Iterators and element access as in `std::vector`, `size`, `capacity`, `max_size` (equal to `capacity`), `empty`, `full`, `clear`, `pop_back`, `resize`, `erase`, `==` and `<=>`. Not provided: `insert`, `assign`, `swap`, `at`.

A full vector cannot grow, so `push_back` and `emplace_back` have the precondition `!full()` (critical contract: writing past the buffer is memory unsafe). `try_push_back` and `try_emplace_back` return `nullptr` instead, for code that cannot know beforehand, such as input from a file. `operator[]`, `front`, `back`, `pop_back` and `erase` have their preconditions as critical contracts as well.
