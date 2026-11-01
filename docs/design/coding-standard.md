# Coding standard

- ISO C++23 only. No compiler extensions, no `__attribute__` or `__declspec` in public headers.
- Avoid macros. They are allowed when nothing else works, but they must not be part of a public header's interface, so the headers can later be exported as a C++ module. The contract macros (`JIO_Expects` and friends) are the only exception, see [design by contract](contracts.md).
- Every public function documents its contract, see [design by contract](contracts.md).
- The protocol operations of `iocore.hpp` are objects (customization points), not functions, so call them qualified and do not name a variable like one in namespace `jetio` (see [io protocol](io-protocol.md)).
- Concept names must not collide with C library functions (`random`) or `std` functions (`forward`), so users can write `using namespace jetio`.
- Headers use `#pragma once` instead of include guards: no macro names to pollute, and not needed in a module. It is not in the ISO standard but all supported compilers implement it.
- Public headers are self-contained and checked by `VERIFY_INTERFACE_HEADER_SETS`.
- Formatting is defined by `.clang-format` (Allman braces, 4 spaces, 100 columns). Run `tools/jetio-dev format`.
- Names: `snake_case` for types, functions and variables, matching the standard library.

## Modules

C++ module support is a goal, not yet implemented. When added, module interface units go under `include/jetio/` or `src/` as a `FILE_SET CXX_MODULES` of the same target, built next to the headers, not instead of them.

## Class design

- Data members have default member initializers and are set in the member initializer list, so no member is ever uninitialized. A constructor checks the invariant after the members are set, but before it reads through a pointer.
- Prefer immutable types: no member changes the object in place; operations return new objects. Assignment and swap replace the whole value. Free functions on such a type take it by const reference or by value, never by non-const reference. This reads well in chains of calls, is easier to get right, and is safe to share between threads.
- A function that does what a standard function of the same name does differently (a new value where the standard changes the object) gets a similar but different, short name, such as `rm_prefix`; it does not get a long one.
- Separate the primitive members from the extensions. A primitive needs the private members, has a meaning that no other member covers, or is needed by the standard algorithms (the iterator functions). Primitives are orthogonal, and no call may break the invariant. An extension is composed from the primitives, for convenience or to follow a convention (`empty`, `front`, ...). Anything that works on the type from outside, such as searching, `substr` or output, is a free function, not a member, to avoid the fat interface of `std::string`.
- Whether a member is a primitive does not follow from its name, but from whether it can be replaced without a loss of meaning or complexity. `empty()` is `size() == 0` for a view, so it is an extension; for a singly linked list it is a primitive, because `size()` is O(n). The members of the STL interface that the algorithms hardly need (`front`, `back`) are style adapters, and with ranges they could be free functions too.
- Private member functions end with an underscore (`scan_()`).
- Contracts in doc comments (`@pre`, `@post`) are written as C++ expressions where that is possible, and in words only for what code cannot say.
- Classes that other headers only need to name have a forward declaration header (`*_fwd.hpp`), which alone gives the default template arguments.
