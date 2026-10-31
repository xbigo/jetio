# Testing

| Kind | Location | Framework | ctest label | Targets |
| --- | --- | --- | --- | --- |
| Unit | `tests/unit` | Catch2 v3 | `unit` | `jetio_unit_tests`, aggregate `jetio_unit`, `jetio_check` |
| Micro-benchmark | `tests/bench` | Google Benchmark | `bench` | `jetio_bench_*`, aggregate `jetio_bench` |
| Fuzz | `tests/fuzz` | libFuzzer (Clang) | `fuzz` | `jetio_fuzz_*`, aggregate `jetio_fuzz` |

- A unit test is fast and repeatable. The whole unit test run must finish in 3 seconds; a slower suite is a project management error to be fixed (split, shrink or move the test to another kind), not a timeout to raise. Repeatability is relaxed slightly: a unit test may do a little IO, but every source of IO data is managed strictly so that the result does not vary between runs and machines. No network, no data from the environment.
- Unit tests serve to check the design of the interface; checking the implementation is secondary. They are not coverage tests, and must not grow into them.
- A function is either a method (it changes state) or a query (it does not). A unit test calls methods to change the state and then checks it with queries; this makes the effects of a method observable, which is how the system stays testable. The invariants of the class are what the unit tests check.
- Common edge cases are covered (empty range, empty container, null pointer). Plain arithmetic overflow is not tested here, unless the code is a saturating arithmetic library; but a size or offset beyond what the platform can hold is a case of boundary safety, which is a design goal of the IO, and gets its basic check.
- Each feature gets a basic check, also where its semantics cannot be described statically (an error code, a state left unchanged after a failure).
- Unit tests show that the thing basically works. Contracts are tested with a custom violation handler that throws (a handler cannot return), to show that kinds and levels are classified correctly; testing the facility itself is a white-box test and special. The default handler, which aborts, is not a unit test subject: if it is tested, it belongs to the extended tests (a category that does not exist yet) because it needs a child process.
- Tests that check contract violations install a handler that throws (`tests/unit/support/contract_guard.hpp`; tests of language extensions are in `tests/unit/std`) and skip themselves when `JIO_CONTRACT_LEVEL` is too low.
- Unit tests are always part of `all`: a build that does not build them is not a build. They are not built when jetio is a subproject (`JETIO_BUILD_TESTS` defaults to OFF there). Benchmarks and fuzzers are part of `all` by default; with `JETIO_BENCH_AND_FUZZ_IN_ALL=OFF` they are built only through their aggregate target.
- Default test presets run `unit` only. Benchmarks run through `*-bench` test presets, fuzzers through the fuzz preset.
- Benchmarks are measured in release presets only. In ctest they run as a short smoke test. `tests/bench/memory.cpp` exercises `memory_device` through the protocol functions; compare a release preset with its `-audit` twin to see the cost of contract checking.

## Unit test time

The `jetio_check` target (`jetio-dev check`) runs the unit tests and fails if the whole run takes longer than `JETIO_UNIT_TEST_TIME_LIMIT` seconds (default 3, 0 disables; set it with `-DJETIO_UNIT_TEST_TIME_LIMIT=<seconds>`). Only the total is checked. When it fails, look at the per-test times of `ctest`, or run `jetio_unit_tests -d yes`.

## Fuzzing and sanitizers

They are orthogonal and kept as separate presets that share one mechanism:

- A fuzzer generates inputs. A sanitizer detects bugs. A fuzzer without a sanitizer only finds crashes and hangs.
- Sanitizer presets (`asan`, `tsan`) run the unit tests. They have no fuzz targets.
- The fuzz preset is Clang with libFuzzer plus ASan and UBSan. TSan is never combined with fuzzing.
- In ctest a fuzz target runs for 5 seconds, or replays `tests/fuzz/corpus/<name>/` if that directory exists. Long runs are started by hand against the executable.
- When a compiled library exists, it must be built with `-fsanitize=fuzzer-no-link` in the fuzz preset. Header-only code is covered because the harness compiles it.

The existing `smoke` fuzzer only checks the infrastructure and is to be replaced by real ones.
