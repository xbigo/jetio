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
# The library: jetio::jetio is compiled (static, or shared with BUILD_SHARED_LIBS), and
# jetio::jetio_header_only is the same library with its implementation included from the
# headers (JIO_HEADER_ONLY). Link one of them, never both: the two do not share definitions.
# ---------------------------------------------------------------------------
# The headers are found by scanning include/; CONFIGURE_DEPENDS re-runs CMake when one is added
# or removed. To leave a file out, list_filter() the variable here.
file(GLOB_RECURSE JETIO_HEADERS LIST_DIRECTORIES false CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.ipp)
file(GLOB_RECURSE JETIO_SOURCES LIST_DIRECTORIES false CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_library(jetio ${JETIO_SOURCES})
add_library(jetio::jetio ALIAS jetio)
add_library(jetio_header_only INTERFACE)
add_library(jetio::jetio_header_only ALIAS jetio_header_only)
target_compile_definitions(jetio_header_only INTERFACE JIO_HEADER_ONLY)

foreach(lib jetio jetio_header_only)
    # PUBLIC on the compiled library, INTERFACE on the header-only one
    get_target_property(type ${lib} TYPE)
    set(scope PUBLIC)
    if(type STREQUAL "INTERFACE_LIBRARY")
        set(scope INTERFACE)
    endif()
    target_sources(${lib} ${scope}
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            ${JETIO_HEADERS})
    target_compile_features(${lib} ${scope} cxx_std_23)
    if(NOT JETIO_CONTRACT_LEVEL STREQUAL "")
        target_compile_definitions(${lib} ${scope} JIO_CONTRACT_LEVEL=${JETIO_CONTRACT_LEVEL})
    endif()
    # 64 bit file offsets on 32 bit POSIX systems
    target_compile_definitions(${lib} ${scope} $<$<NOT:$<PLATFORM_ID:Windows>>:_FILE_OFFSET_BITS=64>)
    # Every public header must compile on its own.
    set_target_properties(${lib} PROPERTIES VERIFY_INTERFACE_HEADER_SETS ON)
endforeach()
target_link_libraries(jetio PRIVATE $<BUILD_INTERFACE:jetio::dev_options>)
set_target_properties(jetio PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
jetio_enable_static_analysis(jetio)

# The target the tests and tools link
option(JETIO_HEADER_ONLY "Build the tests and tools against the header-only library" OFF)
if(JETIO_HEADER_ONLY)
    add_library(jetio_used ALIAS jetio_header_only)
else()
    add_library(jetio_used ALIAS jetio)
endif()

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
