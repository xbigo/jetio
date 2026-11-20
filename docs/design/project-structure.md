# Project structure

```
include/jio/     Public headers, included as <jio/...>; std/ holds language extensions
src/             Sources of the compiled library
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
- There are two library targets with the same headers: `jetio::jetio` is compiled (static, or shared with `BUILD_SHARED_LIBS`), `jetio::jetio_header_only` is an INTERFACE library that defines `JIO_HEADER_ONLY`, which makes the public headers include their implementation (`include/jio/detail/*.ipp`). A program links one of them, never both. The option `JETIO_HEADER_ONLY` selects the one that the tests and tools link. Most of the library is templates and has no compiled part; only the file devices do.
- The headers (`.hpp`, `.ipp`) are a `FILE_SET HEADERS` filled by scanning `include/` (`file(GLOB_RECURSE ... CONFIGURE_DEPENDS)`, so a new header needs no CMake edit and a stray file in the directory is built), so CMake installs them, and `VERIFY_INTERFACE_HEADER_SETS` checks that each header compiles on its own. The sources of the compiled library are every `.cpp` of `src/`, found the same way.
- An implementation lives in an `.ipp` that `src/*.cpp` includes and, with `JIO_HEADER_ONLY`, the public header includes at its end (the way of Boost.Asio, and of fmt and spdlog with their two targets). Its functions are marked `inline` through a macro that exists only inside the `.ipp`. The price of header-only is that the system headers (`<unistd.h>`, `<windows.h>`) come into the translation units of the user. A shared library exports everything: the default visibility on Linux, `WINDOWS_EXPORT_ALL_SYMBOLS` on Windows, so the public headers hold no export macro.
- The project is called JetIO (repository, CMake project and package, target `jetio::jetio`, CMake variables `JETIO_*`, tools), the code is `jio`: namespace `jio`, headers in `<jio/...>`, macros `JIO_*`; `jio/jio.hpp` includes all public headers. `jio` is shorter in symbol names of template heavy code, and no known C++ project uses it.
- Headers: `types.hpp`, `error.hpp`, `iocore.hpp` (protocol), `memory.hpp`, `pseudo.hpp`, `adaptor.hpp`, `cache_view.hpp`, `algorithm.hpp`, `version.hpp`.
- `include/jio/std/` holds the language extensions, things the language or the standard library should have (today: `contract.hpp`, `string_view_concepts.hpp`, `string_view.hpp` (nd, ns and nt), `string_view_io.hpp`, `utility.hpp`, `fixed_vector.hpp`). They do not depend on the rest of the library. Their namespace is `jio` and their macros start with `JIO_`; a `jio::std` namespace would hide the real `std`.

The unit test sources are found the same way: every `.cpp` under `tests/unit/` is a test source. Every `.cpp` under `tests/bench/` is part of the one executable `jetio_benchmarks`, and a subset is run with `--benchmark_filter=<regex>`. Every `.cpp` under `tests/fuzz/` is a fuzz target named after the file (`jetio_fuzz_<name>`), since libFuzzer has one entry point per executable.
