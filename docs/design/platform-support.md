# Platform support

| Platform | Architectures | Status |
| --- | --- | --- |
| Linux | x64 | Supported (GCC, Clang) |
| Linux | x86 | Planned |
| Windows | x64, x86 | Planned |
| WebAssembly | wasm64, wasm32 | Planned |
| macOS | arm64, x64 | Planned |
| iOS | arm64 | Planned |
| Android | arm64, armv7, x64, x86 | Planned |

## Decisions

- Presets are named `<os>-<arch>-<compiler>-<config>[-<variant>]`. New platforms add `<os>-<arch>` hidden presets next to `linux-x64`.
- Platform presets use `condition` on the host system. Cross-compiling presets will use toolchain files under `cmake/toolchains/`.
- No platform detection macros in the library API. Platform differences go behind namespaces and `if constexpr` or `requires`, with platform-specific headers selected by CMake only when unavoidable.
- Code must not assume 64-bit `size_t`, `long`, or pointer width, because x86 and wasm32 are targets.
