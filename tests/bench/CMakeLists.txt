jetio_require_benchmark()

jetio_add_test_executable(jetio_bench_smoke bench
    smoke.cpp)
target_link_libraries(jetio_bench_smoke PRIVATE benchmark::benchmark_main)

# Smoke run only; real measurements: ctest -L bench or run the executable directly.
add_test(NAME bench.smoke
    COMMAND jetio_bench_smoke --benchmark_min_time=0.01s)
set_tests_properties(bench.smoke PROPERTIES LABELS bench)
