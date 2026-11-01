include(CheckCXXSourceCompiles)

set(CMAKE_REQUIRED_FLAGS "-fsanitize=fuzzer")
check_cxx_source_compiles("
    #include <cstddef>
    #include <cstdint>
    extern \"C\" int LLVMFuzzerTestOneInput(const std::uint8_t *, std::size_t) { return 0; }"
    JETIO_HAS_LIBFUZZER)
unset(CMAKE_REQUIRED_FLAGS)

if(NOT JETIO_HAS_LIBFUZZER)
    message(WARNING "JETIO_BUILD_FUZZERS=ON but the compiler has no libFuzzer; skipping fuzz targets")
    return()
endif()
if(NOT JETIO_SANITIZE)
    message(WARNING "Fuzz targets are most useful with JETIO_SANITIZE=\"address;undefined\"")
endif()

# jetio_add_fuzzer(<name> <source>)
#   Registers a libFuzzer target and a short ctest run (label: fuzz).
#   Seed inputs go in tests/fuzz/corpus/<name>/ (optional).
function(jetio_add_fuzzer name source)
    jetio_add_test_executable(jetio_fuzz_${name} fuzz ${source})
    target_compile_options(jetio_fuzz_${name} PRIVATE -fsanitize=fuzzer)
    target_link_options(jetio_fuzz_${name} PRIVATE -fsanitize=fuzzer)

    set(corpus "${CMAKE_CURRENT_SOURCE_DIR}/corpus/${name}")
    if(IS_DIRECTORY "${corpus}")
        add_test(NAME fuzz.${name}
            COMMAND jetio_fuzz_${name} -runs=0 "${corpus}")  # replay seeds only
    else()
        add_test(NAME fuzz.${name}
            COMMAND jetio_fuzz_${name} -max_total_time=5)
    endif()
    set_tests_properties(fuzz.${name} PROPERTIES LABELS fuzz)
endfunction()

# Every .cpp here is a fuzz target named after the file (CONFIGURE_DEPENDS: adding one re-runs
# CMake): libFuzzer has one entry point per executable. To leave a file out, list_filter().
file(GLOB JETIO_FUZZ_SOURCES LIST_DIRECTORIES false CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
foreach(source IN LISTS JETIO_FUZZ_SOURCES)
    cmake_path(GET source STEM name)
    jetio_add_fuzzer(${name} ${source})
endforeach()
