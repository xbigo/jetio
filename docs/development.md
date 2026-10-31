# Development guide

## Requirements

- CMake 3.28 or newer, Ninja
- GCC 14+ or Clang 19+ (the repo is tested with GCC 16 and Clang 21; the libc++ presets use Clang 22 and `libc++-22-dev`)
- Python 3.10+ (stdlib only) for `tools/jetio-dev`
- clang-format for `jetio-dev format`
- Network access on first configure of tests and benchmarks (Catch2 and Google Benchmark are
  fetched unless installed)

## Presets

| Preset | Purpose |
| --- | --- |
| `linux-x64-gcc-debug`, `linux-x64-clang-debug` | Everyday development, unit tests |
| `linux-x64-gcc-release`, `linux-x64-clang-release` | Optimized with debug symbols, unit tests and benchmarks |
| `linux-x64-gcc-release-audit`, `linux-x64-clang-release-audit` | Release with all contract checks (level 3), for comparing benchmarks and finding violations in optimized code |
| `linux-x64-clang-libcxx-debug`, `linux-x64-clang-libcxx-release` | Clang 22 with libc++ instead of libstdc++ |
| `linux-x64-clang-asan` | Unit tests under ASan and UBSan |
| `linux-x64-clang-tsan` | Unit tests under TSan |
| `linux-x64-clang-fuzz` | libFuzzer targets under ASan and UBSan |

Build trees go to `build/<preset>`. Personal presets belong in `CMakeUserPresets.json` (ignored by git).

## Commands

```sh
tools/jetio-dev configure|build|test|check -p <preset>
tools/jetio-dev bench -p linux-x64-gcc-release
tools/jetio-dev format [--check]
tools/jetio-dev clean -p <preset>
```

Useful CMake targets: `jetio_unit`, `jetio_bench`, `jetio_fuzz` (build one kind of test), `jetio_check` (build and run unit tests, fail if they take too long, see [testing](design/testing.md)).

## Editor

VS Code with CMake Tools and clangd (see `.vscode/extensions.json`). CMake Tools copies `compile_commands.json` to the repository root, and clangd falls back to `-std=c++23 -Iinclude` for headers that no translation unit includes yet. The Microsoft C/C++ IntelliSense engine is disabled to avoid clashing with clangd.
