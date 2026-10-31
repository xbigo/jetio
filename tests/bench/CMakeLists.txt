jetio_require_benchmark()

jetio_add_test_executable(jetio_bench_memory bench
    memory.cpp)
target_link_libraries(jetio_bench_memory PRIVATE benchmark::benchmark_main)

# Smoke run only; real measurements: ctest -L bench or run the executable directly.
add_test(NAME bench.memory
    COMMAND jetio_bench_memory --benchmark_min_time=0.01s)
set_tests_properties(bench.memory PROPERTIES LABELS bench)
