# Development guide

## Requirements

On Linux the builds run in a Docker container by default, so Docker is the only requirement: `tools/jetio-dev` makes the image from `tools/docker/Dockerfile` the first time (Ubuntu 26.04 and `tools/setup/linux-apt.sh`) and runs every command in it. The user needs to be in the `docker` group.

To build without Docker, install the same tools natively and pass `--no-docker` (or set `JETIO_DOCKER=0`): on Debian and Ubuntu, `tools/setup/linux-apt.sh` installs them; the image runs the same script, so both have the same requirements.

- CMake 3.28 or newer, Ninja
- GCC 14+ or Clang 19+ (the libc++ presets use Clang 22 and `libc++-22-dev`; the sanitizer and fuzz presets need the compiler-rt of their Clang, `libclang-rt-dev`; the versions in use are in the [build matrix](design/build-matrix.md))
- Python 3.10+ (stdlib only) for `tools/jetio-dev`
- clang-format for `jetio-dev format`
- Network access on first configure of tests and benchmarks (Catch2 and Google Benchmark are
  fetched unless installed)

### The container

The container gets the checkout at the same path as on the host (`-v <checkout>:<checkout>`, working directory the same) and runs as the user and group of the caller (`--user <uid>:<gid>`), so files that it creates belong to the caller and paths in compiler messages, `compile_commands.json` and ctest output are valid on both sides. The image knows no user of that id, which the tools do not need; `HOME` is `/tmp`. Nothing in the image depends on a user, so one image serves everyone.

The build trees of the container are `build/docker/<preset>`, those of the host `build/<preset>`: the `binaryDir` of the presets reads the environment variable `JETIO_BUILD_ENV`, which is set to `docker/` in the container and is empty otherwise, so native tools (VS Code CMake Tools, plain `cmake --preset`) are not affected and the two kinds of tree never mix.

The container runs with the default seccomp profile of Docker plus one rule, `personality(ADDR_NO_RANDOMIZE)`, which TSan needs (`tools/docker/seccomp.json`, see its [README](../tools/docker/README.md)).

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

Build trees go to `build/<preset>` (`build/docker/<preset>` in the container). Personal presets belong in `CMakeUserPresets.json` (ignored by git).

## Commands

```sh
tools/jetio-dev [--no-docker] configure|build|test|check -p <preset>
tools/jetio-dev bench -p linux-x64-gcc-release
tools/jetio-dev format [--check]
tools/jetio-dev clean -p <preset>
```

Useful CMake targets: `jetio_unit`, `jetio_bench`, `jetio_fuzz` (build one kind of test), `jetio_check` (build and run unit tests, fail if they take too long, see [testing](design/testing.md)).

## Editor

VS Code with CMake Tools and clangd (see `.vscode/extensions.json`). CMake Tools copies `compile_commands.json` to the repository root, and clangd falls back to `-std=c++23 -Iinclude` for headers that no translation unit includes yet. The Microsoft C/C++ IntelliSense engine is disabled to avoid clashing with clangd.

## Continuous integration

`.github/workflows/ci.yml` runs on pushes to `main`, on pull requests and on demand. One job per preset on `ubuntu-24.04`, which only provides Docker: each job runs `tools/jetio-dev` (configure, build, test) in the container image, so the toolchain is the one of the image and not the one of the runner. Presets: `gcc-debug`, `clang-debug`, `clang-libcxx-debug`, `gcc-release-audit` (plus the benchmark smoke run), `clang-asan`, `clang-tsan`, `clang-fuzz`.

The image is made by CI: the job `image` pushes `ghcr.io/<owner>/jetio-dev:<hash>` when that hash, which is the one of `tools/docker/Dockerfile` and `tools/setup/linux-apt.sh`, is not in GHCR yet, and the preset jobs pull it (`JETIO_IMAGE_REPO` names the repository; `jetio-dev` pulls a name with a registry before it builds). A change of either file makes the next run build the image once. A pull request from a fork cannot push it.

The container compiles through ccache, in every use of the container, not only in CI. The cache is `build/ccache` of the checkout (limit 500 MB), and CI keeps it per preset with `actions/cache`. A rebuild of a clean tree of `linux-x64-gcc-debug` took 12.5 s without hits and 3.5 s with them (Catch2 is most of the 12.5 s); `tools/jetio-dev ccache` shows the statistics.
