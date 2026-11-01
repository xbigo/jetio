jetio_require_benchmark()

# Every .cpp here registers its benchmarks into one executable (CONFIGURE_DEPENDS: adding a file
# re-runs CMake); pick some with --benchmark_filter=<regex>. To leave a file out, list_filter().
file(GLOB_RECURSE JETIO_BENCH_SOURCES LIST_DIRECTORIES false CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
jetio_add_test_executable(jetio_benchmarks bench ${JETIO_BENCH_SOURCES})
target_link_libraries(jetio_benchmarks PRIVATE benchmark::benchmark_main)

# Smoke run only; real measurements: ctest -L bench or run the executable directly.
add_test(NAME bench.all
    COMMAND jetio_benchmarks --benchmark_min_time=0.01s)
set_tests_properties(bench.all PROPERTIES LABELS bench)
