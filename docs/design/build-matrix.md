# Build matrix

Results of the builds and tests, and the toolchain behind them. The measured part is Linux x64; the other platforms are kept as rows to fill in when they are first built (see [platform support](platform-support.md)).

## Environments

Linux builds run in the Docker image of `tools/docker` by default (see the [development guide](../development.md)); the `native` column is the same presets with the tools of a host, measured before the switch.

| Item | docker | native |
| --- | --- | --- |
| OS | Ubuntu 26.04.1 LTS | Debian GNU/Linux forky/sid |
| GCC | 15.2.0 (Ubuntu 15.2.0-16ubuntu1) | 16.2.0 (Debian 16.2.0-3) |
| Clang (`clang++`), clang-format, clang-tidy | 21.1.8 (Ubuntu 21.1.8-6ubuntu1) | 21.1.8 (Debian 21.1.8-13) |
| Clang (`clang++-22`, libc++ presets) | 22.1.2 (Ubuntu 22.1.2-1ubuntu1) | 22.1.8 (Debian 1+b2) |
| CMake, Ninja, Python | 4.2.3, 1.13.2, 3.14.4 | 4.3.4, 1.13.2, 3.14.8 |
| Catch2, Google Benchmark | v3.16.1, v1.10.0 (fetched) | the same |
| Machine | AMD Ryzen 7 7840H, 16 threads, 16 GB | the same |

## C and C++ runtimes

The runtime decides the ABI of the binaries, and the version of the headers decides which standard library features exist, so both are recorded.

| Presets | C++ library | ABI marker | C library and unwinder |
| --- | --- | --- | --- |
| docker, `gcc-*` | libstdc++ headers of GCC 15.2.0-16 (`_GLIBCXX_RELEASE` 15, `__GLIBCXX__` 20260321); the library at run time is libstdc++6 16-20260322-1ubuntu1 | `_GLIBCXX_USE_CXX11_ABI` 1; the library exports up to `GLIBCXX_3.4.35` and `CXXABI_1.3.17` | glibc 2.43-2ubuntu2.4, libgcc_s 16-20260322-1ubuntu1 |
| docker, `clang-*`, `clang-asan`, `clang-tsan`, `clang-fuzz` | libstdc++ of GCC 15 (Clang selects the GCC 15 installation), as above | as above | as above; the sanitizer runtime is the one of the compiler (libclang-rt-dev 21.1.6-71) |
| docker, `clang-libcxx-*` | libc++ 22.1.2 (`_LIBCPP_VERSION` 220102) and libc++abi 22.1.2 | `_LIBCPP_ABI_VERSION` 1, namespace `std::__1` | as above (libgcc_s is the unwinder) |
| native, `gcc-*` | libstdc++ 16.2.0-3 (`_GLIBCXX_RELEASE` 16, `__GLIBCXX__` 20260918) | `_GLIBCXX_USE_CXX11_ABI` 1; up to `GLIBCXX_3.4.36` and `CXXABI_1.3.17` | glibc 2.43-5, libgcc_s 16.2.0-3 |
| native, `clang-*` and sanitizers | libstdc++ of GCC 16, as above | as above | as above |
| native, `clang-libcxx-*` | libc++ 22.1.8 (`_LIBCPP_VERSION` 220108) and libc++abi 22.1.8 | `_LIBCPP_ABI_VERSION` 1, namespace `std::__1` | glibc 2.43-5, libgcc_s 16.2.0-3 |

The two C++ libraries are never mixed: a preset builds the tests and the fetched dependencies with its own library.

## Linux x64, measured

Each configure preset was configured with `--fresh` and built with `--clean-first`, then tested. No compiler cache, Ninja on all 16 threads, one run each, one after the other. Times are seconds on the machine above. Only the relations between them are meant to carry over to other machines. In the docker columns every command also starts a container, which costs about 0.3 s: the unit tests take 0.3 s natively and 0.6 s in the container.

| Preset | Runs | Configure (docker) | Build (docker) | Build relative | Test (docker) | Build (native) |
| --- | --- | --- | --- | --- | --- | --- |
| `linux-x64-gcc-debug` | unit | 0.6 | 11.1 | 1.00 | 0.6 | 11.7 |
| `linux-x64-gcc-release` | unit, bench | 4.7 | 16.0 | 1.44 | 0.6 | 16.8 |
| `linux-x64-gcc-release-audit` | unit, bench | 5.3 | 16.0 | 1.44 | 0.6 | 17.0 |
| `linux-x64-clang-debug` | unit | 2.0 | 13.6 | 1.23 | 0.6 | 13.9 |
| `linux-x64-clang-release` | unit, bench | 5.3 | 17.3 | 1.56 | 0.6 | 17.6 |
| `linux-x64-clang-release-audit` | unit, bench | 6.3 | 17.6 | 1.59 | 0.8 | 17.6 |
| `linux-x64-clang-libcxx-debug` | unit | 2.1 | 13.4 | 1.21 | 0.6 | 11.9 |
| `linux-x64-clang-libcxx-release` | unit, bench | 6.4 | 17.2 | 1.55 | 0.7 | 15.9 |
| `linux-x64-clang-asan` (ASan+UBSan) | unit | 1.8 | 13.8 | 1.24 | 2.2 | 14.0 |
| `linux-x64-clang-tsan` (TSan) | unit | 1.8 | 13.4 | 1.21 | 2.6 | 14.1 |
| `linux-x64-clang-fuzz` (libFuzzer+ASan+UBSan) | fuzz | 1.6 | 14.3 | 1.29 | 6.6 | 14.6 |

The benchmark test presets (`*-bench`, a short smoke run of the benchmarks built by the release presets) take 0.4 to 0.5 s each in the container, 0.2 s natively.

What the numbers say:

- The whole matrix in the container, one preset after the other, takes about 218 s: builds 164 s (75 %), configure 38 s (17 %), tests 14 s (6 %), benchmark smoke runs 2 s, without the TSan tests. Natively it was about 198 s.
- The build dominates everywhere. The unit tests are 0.3 s natively in the plain presets, ASan makes them about 6 times longer, TSan about 9 times.
- Release presets build 1.4 to 1.6 times as long as debug ones and configure in 4.7 to 6.4 s instead of 0.6 to 2.1 s, since they also configure the benchmarks (Google Benchmark). The audit presets cost the same as their release twins to build and test.
- Debug builds with GCC and with libc++ are the cheapest, Clang with libstdc++ about 1.2 times.
- The fuzz test run is 5 s by design (see [testing](testing.md)).
- Builds in the container take as long as native ones, within a second or two.

TSan in a container: with the default seccomp profile of Docker the tests of the TSan preset exit at start with "ThreadSanitizer: encountered an incompatible memory layout but was unable to disable ASLR". The kernel of the machine randomizes addresses more than TSan's memory layout allows, TSan switches ASLR off for itself with `personality(ADDR_NO_RANDOMIZE)`, and the default profile does not allow that call. The profile `tools/docker/seccomp.json` adds that one call and the 129 tests pass in the container (the machine is an LXC container running Docker; the call is allowed there).

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
