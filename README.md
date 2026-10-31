# jetio

A simple C++ IO library. Header-only for now, ISO C++23.

## Quick start

```sh
tools/jetio-dev list                          # list presets
tools/jetio-dev check -p linux-x64-gcc-debug  # configure, build, test
```

Or use CMake directly:

```sh
cmake --preset linux-x64-gcc-debug
cmake --build --preset linux-x64-gcc-debug
ctest --preset linux-x64-gcc-debug
```

Consume it from another CMake project:

```cmake
find_package(jetio CONFIG REQUIRED)
target_link_libraries(app PRIVATE jetio::jetio)
```

## Documentation

See [docs/](docs/README.md).

## License

MIT, see [LICENSE](LICENSE).
