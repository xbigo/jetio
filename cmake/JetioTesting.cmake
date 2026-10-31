# Helpers shared by tests/unit, tests/bench and tests/fuzz.
#
# jetio_add_test_executable(<target> <unit|bench|fuzz> <sources...>)
#   Creates an executable wired to the library, the dev options and the
#   aggregate target jetio_<group>. Unit tests are always part of 'all'. With
#   JETIO_BENCH_AND_FUZZ_IN_ALL=OFF, benchmarks and fuzzers are only built
#   through their aggregate target.

function(jetio_add_test_executable target group)
    add_executable(${target} ${ARGN})
    target_link_libraries(${target} PRIVATE jetio::jetio jetio::dev_options)
    if(NOT group STREQUAL "unit" AND NOT JETIO_BENCH_AND_FUZZ_IN_ALL)
        set_target_properties(${target} PROPERTIES EXCLUDE_FROM_ALL TRUE)
    endif()
    add_dependencies(jetio_${group} ${target})
    jetio_enable_static_analysis(${target})
endfunction()
