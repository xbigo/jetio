# Project structure

```
include/jio/     Public headers, included as <jio/...>; std/ holds language extensions
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

- Public headers and private sources are separate trees. Files in `src/` include public headers with `<jio/...>`, never by relative path.
- The library target is `jetio::jetio`, an INTERFACE library. Its headers are a `FILE_SET HEADERS` filled by scanning `include/` (`file(GLOB_RECURSE ... CONFIGURE_DEPENDS)`, so a new header needs no CMake edit and a stray file in the directory is built), so CMake installs them, and `VERIFY_INTERFACE_HEADER_SETS` checks that each header compiles on its own.
- When a compiled part appears, it is added under `src/` and linked from the `jetio` target, so the consumer-facing target name does not change.
- The project is called JetIO (repository, CMake project and package, target `jetio::jetio`, CMake variables `JETIO_*`, tools), the code is `jio`: namespace `jio`, headers in `<jio/...>`, macros `JIO_*`; `jio/jio.hpp` includes all public headers. `jio` is shorter in symbol names of template heavy code, and no known C++ project uses it.
- Headers: `types.hpp`, `error.hpp`, `iocore.hpp` (protocol), `memory.hpp`, `pseudo.hpp`, `adaptor.hpp`, `cache_view.hpp`, `algorithm.hpp`, `version.hpp`.
- `include/jio/std/` holds the language extensions, things the language or the standard library should have (today: `contract.hpp`, `string_view_concepts.hpp`, `string_view.hpp` (nd, ns and nt), `string_view_io.hpp`, `utility.hpp`, `fixed_vector.hpp`). They do not depend on the rest of the library. Their namespace is `jio` and their macros start with `JIO_`; a `jio::std` namespace would hide the real `std`.

The unit test sources are found the same way: every `.cpp` under `tests/unit/` is a test source. Every `.cpp` under `tests/bench/` is part of the one executable `jetio_benchmarks`, and a subset is run with `--benchmark_filter=<regex>`. Every `.cpp` under `tests/fuzz/` is a fuzz target named after the file (`jetio_fuzz_<name>`), since libFuzzer has one entry point per executable.
