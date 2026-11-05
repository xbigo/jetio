# Build matrix

Results of the builds and tests, and the toolchain behind them. The measured part is Linux x64; the other platforms are kept as rows to fill in when they are first built (see [platform support](platform-support.md)).

## Toolchain of the measurements

| Item | Version |
| --- | --- |
| OS | Debian GNU/Linux forky/sid, x86_64 |
| Machine | AMD Ryzen 7 7840H, 16 threads, 16 GB |
| GCC | 16.2.0 (Debian 16.2.0-3) |
| Clang (`clang++`) | 21.1.8 (Debian 21.1.8-13), uses the GCC 16 installation for its C++ library |
| Clang (`clang++-22`, libc++ presets) | 22.1.8 (Debian 1+b2) |
| CMake, Ninja, Python | 4.3.4, 1.13.2, 3.14.8 |
| Catch2, Google Benchmark | v3.16.1, v1.10.0 (fetched) |

## C and C++ runtimes

The runtime decides the ABI of the binaries, and the version of the headers decides which standard library features exist, so both are recorded.

| Presets | C++ library | ABI marker | C library and unwinder |
| --- | --- | --- | --- |
| `gcc-*` | libstdc++ 16.2.0-3 (`_GLIBCXX_RELEASE` 16, `__GLIBCXX__` 20260918) | `_GLIBCXX_USE_CXX11_ABI` 1; the library exports up to `GLIBCXX_3.4.36` and `CXXABI_1.3.17` | glibc 2.43-5, libgcc_s 16.2.0-3 |
| `clang-*`, `clang-asan`, `clang-tsan`, `clang-fuzz` | libstdc++ of GCC 16, as above | as above | as above; the sanitizer runtime is the one of the compiler |
| `clang-libcxx-*` | libc++ 22.1.8 (`_LIBCPP_VERSION` 220108) and libc++abi 22.1.8 | `_LIBCPP_ABI_VERSION` 1, namespace `std::__1` | glibc 2.43-5, libgcc_s 16.2.0-3 (the unwinder) |

The two C++ libraries are never mixed: a preset builds the tests and the fetched dependencies with its own library.

## Linux x64, measured

Each configure preset was configured with `cmake --fresh` and built with `cmake --build --clean-first`, then tested. No compiler cache, Ninja on all 16 threads, one run each, one after the other. Times are seconds on the machine above. Only the relations between them are meant to carry over to other machines.

| Preset | Compiler, library | Runs | Configure | Build | Build relative | Test | Test relative |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `linux-x64-gcc-debug` | GCC, libstdc++ | unit | 0.4 | 11.7 | 1.00 | 0.3 | 1 |
| `linux-x64-gcc-release` | GCC, libstdc++ | unit, bench | 2.7 | 16.8 | 1.44 | 0.3 | 1 |
| `linux-x64-gcc-release-audit` | GCC, libstdc++ | unit, bench | 2.7 | 17.0 | 1.45 | 0.3 | 1 |
| `linux-x64-clang-debug` | Clang, libstdc++ | unit | 0.5 | 13.9 | 1.19 | 0.3 | 1 |
| `linux-x64-clang-release` | Clang, libstdc++ | unit, bench | 3.3 | 17.6 | 1.50 | 0.3 | 1 |
| `linux-x64-clang-release-audit` | Clang, libstdc++ | unit, bench | 3.3 | 17.6 | 1.50 | 0.3 | 1 |
| `linux-x64-clang-libcxx-debug` | Clang 22, libc++ | unit | 0.4 | 11.9 | 1.02 | 0.3 | 1 |
| `linux-x64-clang-libcxx-release` | Clang 22, libc++ | unit, bench | 3.8 | 15.9 | 1.36 | 0.3 | 1 |
| `linux-x64-clang-asan` | Clang, libstdc++, ASan+UBSan | unit | 0.5 | 14.0 | 1.20 | 1.9 | 6 |
| `linux-x64-clang-tsan` | Clang, libstdc++, TSan | unit | 0.5 | 14.1 | 1.21 | 2.6 | 9 |
| `linux-x64-clang-fuzz` | Clang, libstdc++, libFuzzer+ASan+UBSan | fuzz | 0.6 | 14.6 | 1.25 | 6.3 | 21 |

The benchmark test presets (`*-bench`, a short smoke run of the benchmarks built by the release presets) take 0.2 s each: `gcc-release`, `clang-release`, `clang-libcxx-release`, `gcc-release-audit`, `clang-release-audit`.

What the numbers say:

- The whole matrix, one preset after the other, takes about 198 s: builds 165 s (83 %), configure 19 s (9 %), tests 13 s (7 %), benchmark smoke runs 1 s.
- The build dominates everywhere. The unit tests are 0.3 s in the plain presets and 6 to 9 times that under the sanitizers.
- Release presets build 1.4 to 1.5 times as long as debug ones and configure in 2.7 to 3.8 s instead of 0.4 to 0.6 s, since they also configure the benchmarks (Google Benchmark). The audit presets cost the same as their release twins to build and test.
- Debug builds with GCC and with libc++ are the cheapest, Clang with libstdc++ about 1.2 times.
- The fuzz test run is 5 s by design (see [testing](testing.md)).

## Other platforms

Not built yet. Versions are recorded in the first row of each platform when it is first built, in the same two tables as above.

| Platform | Architectures | Compilers | C++ library | C library | Status |
| --- | --- | --- | --- | --- | --- |
| Linux | x64 | GCC, Clang | libstdc++, libc++ | glibc | Measured, above |
| Linux | x86 | GCC, Clang | libstdc++, libc++ | glibc | Planned |
| Linux | arm64, riscv64 | GCC, Clang (cross, Docker, `qemu-user`) | libstdc++ | glibc | Planned |
| Windows | x64, x86 | MSVC, clang-cl | MSVC STL | UCRT | Planned |
| Windows | arm64 | MSVC, clang-cl | MSVC STL | UCRT | Planned |
| WebAssembly | wasm32, wasm64 | Emscripten (Clang) | libc++ | Emscripten libc (musl based) | Planned |
| macOS | arm64 | Apple Clang | libc++ (system) | libSystem | Planned |
| iOS | arm64 | Apple Clang | libc++ (system) | libSystem | Planned |
| Android | arm64, armv7, x64, x86 | NDK Clang | libc++ (NDK) | bionic | Planned |

Open for the rows: whether MinGW-w64 GCC is a Windows target, and musl on Linux, are not decided.

What to record for each cell, besides the compiler version: `_GLIBCXX_RELEASE` and `_GLIBCXX_USE_CXX11_ABI` (libstdc++), `_LIBCPP_VERSION` and `_LIBCPP_ABI_VERSION` (libc++), `_MSVC_STL_VERSION` (MSVC STL), the C library version (glibc, UCRT, bionic API level, macOS or iOS deployment target) and the unwinder where it is separate.
