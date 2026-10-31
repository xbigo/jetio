# Runs the unit tests and fails if the whole run takes longer than the limit (seconds, 0 = no
# limit). Used by the jetio_check target:
#   cmake -DCTEST=<ctest> -DBINARY_DIR=<dir> -DCONFIG=<config> -DLIMIT=<seconds> -P JetioTimedCheck.cmake

execute_process(
    COMMAND "${CTEST}" --output-on-failure -C "${CONFIG}" -L unit
    WORKING_DIRECTORY "${BINARY_DIR}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ECHO_OUTPUT_VARIABLE)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Unit tests failed")
endif()

if(NOT output MATCHES "Total Test time \\(real\\) = +([0-9.]+) sec")
    message(WARNING "Total test time not found in the ctest output, the time limit is not checked")
    return()
endif()
set(total "${CMAKE_MATCH_1}")
if(LIMIT GREATER 0 AND total GREATER LIMIT)
    message(FATAL_ERROR "Unit tests took ${total} s, the limit is ${LIMIT} s (JETIO_UNIT_TEST_TIME_LIMIT)")
endif()
