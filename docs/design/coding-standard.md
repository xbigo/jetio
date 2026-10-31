# Coding standard

- ISO C++23 only. No compiler extensions, no `__attribute__` or `__declspec` in public headers.
- Avoid macros. They are allowed when nothing else works, but they must not be part of a public header's interface, so the headers can later be exported as a C++ module.
- Headers use `#pragma once` instead of include guards: no macro names to pollute, and not needed in a module. It is not in the ISO standard but all supported compilers implement it.
- Public headers are self-contained and checked by `VERIFY_INTERFACE_HEADER_SETS`.
- Formatting is defined by `.clang-format` (Allman braces, 4 spaces, 100 columns). Run `tools/jetio-dev format`.
- Names: `snake_case` for types, functions and variables, matching the standard library.

## Modules

C++ module support is a goal, not yet implemented. When added, module interface units go under `include/jetio/` or `src/` as a `FILE_SET CXX_MODULES` of the same target, built next to the headers, not instead of them.
