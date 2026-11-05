# Platform support

| Platform | Architectures | Status |
| --- | --- | --- |
| Linux | x64 | Supported (GCC, Clang) |
| Linux | x86 | Planned |
| Linux | arm64, riscv64 | Planned |
| Windows | x64, x86 | Planned |
| Windows on Arm | arm64 | Planned |
| WebAssembly | wasm64, wasm32 | Planned |
| macOS | arm64 | Planned |
| iOS | arm64 | Planned |
| Android | arm64, armv7, x64, x86 | Planned |

macOS on x64 is deliberately not supported. 32 bit is planned for Windows, Linux x86 and WebAssembly; the bitness of Linux on Arm and RISC-V beyond arm64 and riscv64 is not decided.

The toolchains, runtimes and measured build and test times are in the [build matrix](build-matrix.md).

## Decisions

- Presets are named `<os>-<arch>-<compiler>-<config>[-<variant>]`. New platforms add `<os>-<arch>` hidden presets next to `linux-x64`.
- Platform presets use `condition` on the host system. Cross-compiling presets will use toolchain files under `cmake/toolchains/`.
- Linux arm64 is cross-compiled in a Docker environment. Regular CI runs only a minimal verification under `qemu-user`; full test runs need native hardware.
- No platform detection macros in the library API. Platform differences go behind namespaces and `if constexpr` or `requires`, with platform-specific headers selected by CMake only when unavoidable.
- Code must not assume 64-bit `size_t`, `long`, or pointer width, because x86 and wasm32 are targets.
