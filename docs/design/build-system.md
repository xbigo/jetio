# Build system

- CMake 3.28+, Ninja, CMake presets (version 6) as the single entry point for configure, build and test.
- Python is a convenience layer only (`tools/jetio-dev`). It calls presets and never replaces CMake logic.
- Standard: ISO C++23 with `CMAKE_CXX_EXTENSIONS OFF` (`-std=c++23`, not `gnu++23`). GCC and Clang also get `-Wpedantic -pedantic-errors`, so compiler extensions are errors. MSVC gets `/permissive-`.
- Project-owned targets link `jetio::dev_options` for warnings, sanitizers and debug symbols. It is never installed, so it cannot leak into consumers.
- Standard library: libstdc++ by default. The `libcxx` presets build everything, dependencies included, with `-stdlib=libc++` so both implementations are tested. They pin `clang++-22` because libc++ headers and compiler should come from the same LLVM release.
- Release builds keep debug symbols (`-g`, or `/Zi /DEBUG` on MSVC).
- Debug builds enable the standard library assertions (`_GLIBCXX_ASSERTIONS`, libc++ extensive hardening).
- `JETIO_SANITIZE` accepts `address`, `undefined`, `thread`, `memory`. Thread cannot be combined with address or memory.
- Dependencies: an installed Catch2 or Google Benchmark is preferred; otherwise a pinned version is fetched with `FetchContent`.

## Options

| Option | Default | Meaning |
| --- | --- | --- |
| `JETIO_BUILD_TESTS` | ON at top level | Unit tests |
| `JETIO_BUILD_BENCHMARKS` | OFF (ON in release presets) | Micro-benchmarks |
| `JETIO_BUILD_FUZZERS` | OFF | Fuzz targets, needs libFuzzer |
| `JETIO_BUILD_TOOLS` | ON at top level | C++ tools |
| `JETIO_INSTALL` | ON at top level | Install rules and package config |
| `JETIO_BENCH_AND_FUZZ_IN_ALL` | ON | Build benchmarks and fuzzers in `all`. Unit tests are always in `all` |
| `JETIO_WARNINGS_AS_ERRORS` | OFF | `-Werror` / `/WX` |
| `JETIO_CONTRACT_LEVEL` | empty | Contract checks: 0 off, 1 critical, 2 plus default, 3 plus audit; empty is 1 with `NDEBUG`, else 3 |
| `JETIO_SANITIZE` | empty | Sanitizer list |
| `JETIO_ENABLE_CLANG_TIDY` | OFF | Static analysis placeholder |

## Static analysis

Placeholder in `cmake/JetioStaticAnalysis.cmake` (`jetio_enable_static_analysis`) and `.clang-tidy`. Only clang-tidy is wired up. Add cppcheck, include-what-you-use and MSVC `/analyze` there.
