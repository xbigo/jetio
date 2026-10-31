# Testing

| Kind | Location | Framework | ctest label | Targets |
| --- | --- | --- | --- | --- |
| Unit | `tests/unit` | Catch2 v3 | `unit` | `jetio_unit_tests`, aggregate `jetio_unit`, `jetio_check` |
| Micro-benchmark | `tests/bench` | Google Benchmark | `bench` | `jetio_bench_*`, aggregate `jetio_bench` |
| Fuzz | `tests/fuzz` | libFuzzer (Clang) | `fuzz` | `jetio_fuzz_*`, aggregate `jetio_fuzz` |

- Tests are part of `all` by default. With `JETIO_TEST_TARGETS_IN_ALL=OFF` they are built only through their aggregate target.
- Default test presets run `unit` only. Benchmarks run through `*-bench` test presets, fuzzers through the fuzz preset.
- Benchmarks are measured in release presets only. In ctest they run as a short smoke test.

## Fuzzing and sanitizers

They are orthogonal and kept as separate presets that share one mechanism:

- A fuzzer generates inputs. A sanitizer detects bugs. A fuzzer without a sanitizer only finds crashes and hangs.
- Sanitizer presets (`asan`, `tsan`) run the unit tests. They have no fuzz targets.
- The fuzz preset is Clang with libFuzzer plus ASan and UBSan. TSan is never combined with fuzzing.
- In ctest a fuzz target runs for 5 seconds, or replays `tests/fuzz/corpus/<name>/` if that directory exists. Long runs are started by hand against the executable.
- When a compiled library exists, it must be built with `-fsanitize=fuzzer-no-link` in the fuzz preset. Header-only code is covered because the harness compiles it.

The existing `smoke` benchmark and fuzzer only check the infrastructure and are to be replaced by real ones.
