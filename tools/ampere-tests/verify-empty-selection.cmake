# SPDX-License-Identifier: GPL-3.0-or-later

if(NOT DEFINED TEST_EXECUTABLE OR NOT EXISTS "${TEST_EXECUTABLE}")
    message(FATAL_ERROR "TEST_EXECUTABLE must name the built ampere-quick-tests executable")
endif()
execute_process(
    COMMAND "${TEST_EXECUTABLE}" "[ampere-quick-tests-deliberately-absent]"
            --allow-running-no-tests
    RESULT_VARIABLE status
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
if(status STREQUAL "0")
    message(FATAL_ERROR "An empty test selection incorrectly succeeded")
endif()
if(NOT "${output}${error}" MATCHES "No test cases matched")
    message(FATAL_ERROR "Expected an empty-selection diagnostic, got ${status}:\n${output}${error}")
endif()
