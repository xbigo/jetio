jetio_require_catch2()
include(Catch)

# Every .cpp here is a unit test source (CONFIGURE_DEPENDS: adding one re-runs CMake). To leave
# a file out, list_filter() the variable.
file(GLOB_RECURSE JETIO_UNIT_TEST_SOURCES LIST_DIRECTORIES false CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
jetio_add_test_executable(jetio_unit_tests unit ${JETIO_UNIT_TEST_SOURCES})
target_include_directories(jetio_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
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
