# Project structure

```
include/jetio/   Public headers, included as <jetio/...>; std/ holds language extensions
src/             Private sources and headers (empty while header-only)
tests/unit/      Catch2 unit tests
tests/bench/     Google Benchmark micro-benchmarks
tests/fuzz/      libFuzzer targets
tools/cpp/       C++ command-line tools
tools/py/        Python commands (jetio-dev)
cmake/           Project CMake modules and package config template
docs/            Documentation
```

## Decisions

- Public headers and private sources are separate trees. Files in `src/` include public headers with `<jetio/...>`, never by relative path.
- The library target is `jetio::jetio`, an INTERFACE library. Its headers are a `FILE_SET HEADERS` filled by scanning `include/` (`file(GLOB_RECURSE ... CONFIGURE_DEPENDS)`, so a new header needs no CMake edit and a stray file in the directory is built), so CMake installs them, and `VERIFY_INTERFACE_HEADER_SETS` checks that each header compiles on its own.
- When a compiled part appears, it is added under `src/` and linked from the `jetio` target, so the consumer-facing target name does not change.
- Everything lives in namespace `jetio`; `jetio/jetio.hpp` includes all public headers.
- Headers: `types.hpp`, `error.hpp`, `iocore.hpp` (protocol), `memory.hpp`, `pseudo.hpp`, `version.hpp`.
- `include/jetio/std/` holds the language extensions, things the language or the standard library should have (today: `contract.hpp`, `string_view_concepts.hpp`, `string_view.hpp` (nd, ns and nt), `string_view_io.hpp`, `utility.hpp`). They do not depend on the rest of the library. Their namespace is `jetio` and their macros start with `JIO_`; a `jetio::std` namespace would hide the real `std`.

The unit test sources are found the same way: every `.cpp` under `tests/unit/` is a test source. Benchmarks and fuzz targets are listed by name, one target per source.
