cmake_minimum_required(VERSION 3.28)

project(jetio
    VERSION 0.1.0
    DESCRIPTION "A simple C++ IO library"
    HOMEPAGE_URL "https://github.com/xbigo/jetio"
    LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(JETIO_BUILD_TESTS "Build the unit tests" ${PROJECT_IS_TOP_LEVEL})
option(JETIO_BUILD_BENCHMARKS "Build the micro-benchmarks" OFF)
option(JETIO_BUILD_FUZZERS "Build the fuzz targets (needs clang with libFuzzer)" OFF)
option(JETIO_BUILD_TOOLS "Build the command-line tools" ${PROJECT_IS_TOP_LEVEL})
option(JETIO_INSTALL "Generate install rules" ${PROJECT_IS_TOP_LEVEL})
option(JETIO_BENCH_AND_FUZZ_IN_ALL
    "Build benchmarks and fuzzers as part of the default 'all' target (unit tests always are)" ON)
option(JETIO_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(JETIO_ENABLE_CLANG_TIDY "Run clang-tidy on project targets (static analysis placeholder)" OFF)
set(JETIO_UNIT_TEST_TIME_LIMIT 3 CACHE STRING
    "Seconds the whole unit test run may take in the jetio_check target; 0 disables the limit")
set(JETIO_CONTRACT_LEVEL "" CACHE STRING
    "Contract checks: 0 = off, 1 = critical only, 2 = also default, 3 = also audit; empty = 1 with NDEBUG, else 3")
set_property(CACHE JETIO_CONTRACT_LEVEL PROPERTY STRINGS "" 0 1 2 3)
set(JETIO_SANITIZE "" CACHE STRING
    "Semicolon-separated sanitizers for project targets: address, undefined, thread, memory")

# ---------------------------------------------------------------------------
# Language standard: ISO C++23, no compiler extensions (-std=c++23, not gnu++23)
# ---------------------------------------------------------------------------
if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_CXX_STANDARD 23)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)
endif()

include(JetioDevOptions)
include(JetioStaticAnalysis)

# ---------------------------------------------------------------------------
# The library (header-only for now)
# ---------------------------------------------------------------------------
add_library(jetio INTERFACE)
add_library(jetio::jetio ALIAS jetio)
target_sources(jetio INTERFACE
    FILE_SET HEADERS
    BASE_DIRS include
    FILES
        include/jetio/contract.hpp
        include/jetio/error.hpp
        include/jetio/iocore.hpp
        include/jetio/jetio.hpp
        include/jetio/memory.hpp
        include/jetio/pseudo.hpp
        include/jetio/types.hpp
        include/jetio/version.hpp)
target_compile_features(jetio INTERFACE cxx_std_23)
if(NOT JETIO_CONTRACT_LEVEL STREQUAL "")
    target_compile_definitions(jetio INTERFACE JIO_CONTRACT_LEVEL=${JETIO_CONTRACT_LEVEL})
endif()
# Every public header must compile on its own.
set_target_properties(jetio PROPERTIES VERIFY_INTERFACE_HEADER_SETS ON)

# Future compiled part: add src/CMakeLists.txt and target_link_libraries(jetio INTERFACE ...)
# once the first translation unit lands in src/.

# ---------------------------------------------------------------------------
# Tests, tools, install
# ---------------------------------------------------------------------------
if(JETIO_BUILD_TESTS OR JETIO_BUILD_BENCHMARKS OR JETIO_BUILD_FUZZERS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(JETIO_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(JETIO_INSTALL)
    include(JetioInstall)
endif()
