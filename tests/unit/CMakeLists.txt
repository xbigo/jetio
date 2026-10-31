jetio_require_catch2()
include(Catch)

jetio_add_test_executable(jetio_unit_tests unit
    contract.cpp
    error.cpp
    iocore.cpp
    memory.cpp
    pseudo.cpp
    types.cpp
    version.cpp)
target_link_libraries(jetio_unit_tests PRIVATE Catch2::Catch2WithMain)
target_compile_definitions(jetio_unit_tests PRIVATE
    JETIO_TEST_EXPECTED_VERSION="${PROJECT_VERSION}")

catch_discover_tests(jetio_unit_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS unit)

# Build and run the unit tests, and check that they are fast enough:
#   cmake --build <dir> --target jetio_check
add_custom_target(jetio_check
    COMMAND ${CMAKE_COMMAND}
        -DCTEST=${CMAKE_CTEST_COMMAND}
        -DBINARY_DIR=${PROJECT_BINARY_DIR}
        -DCONFIG=$<CONFIG>
        -DLIMIT=${JETIO_UNIT_TEST_TIME_LIMIT}
        -P ${PROJECT_SOURCE_DIR}/cmake/JetioTimedCheck.cmake
    DEPENDS jetio_unit_tests
    USES_TERMINAL)
