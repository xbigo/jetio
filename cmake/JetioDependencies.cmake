# Third-party dependencies for tests and benchmarks. An installed package is
# preferred; otherwise the pinned version is fetched.

include(FetchContent)

function(jetio_require_catch2)
    if(TARGET Catch2::Catch2WithMain)
        return()
    endif()
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.16.1
        GIT_SHALLOW TRUE
        SYSTEM
        FIND_PACKAGE_ARGS 3 CONFIG)
    set(CATCH_INSTALL_DOCS OFF)
    set(CATCH_INSTALL_EXTRAS OFF)
    FetchContent_MakeAvailable(Catch2)
    if(Catch2_SOURCE_DIR)
        list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")
        set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" PARENT_SCOPE)
    endif()
endfunction()

function(jetio_require_benchmark)
    if(TARGET benchmark::benchmark_main)
        return()
    endif()
    FetchContent_Declare(benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG v1.10.0
        GIT_SHALLOW TRUE
        SYSTEM
        FIND_PACKAGE_ARGS 1.9 CONFIG)
    set(BENCHMARK_ENABLE_TESTING OFF)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF)
    set(BENCHMARK_ENABLE_INSTALL OFF)
    set(BENCHMARK_ENABLE_WERROR OFF)
    FetchContent_MakeAvailable(benchmark)
endfunction()
