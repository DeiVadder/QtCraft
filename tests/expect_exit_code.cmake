# Runs COMMAND (a ;-list) and fails unless it exits with EXPECTED_EXIT_CODE and, if given, prints
# EXPECTED_OUTPUT (standard output) and EXPECTED_ERROR (standard error). Unlike WILL_FAIL this does not accept crashes or timeouts as success.
execute_process(COMMAND ${COMMAND} RESULT_VARIABLE exit_code OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT exit_code STREQUAL EXPECTED_EXIT_CODE)
    message(FATAL_ERROR "Expected exit code ${EXPECTED_EXIT_CODE}, got '${exit_code}'\n${output}\n${errors}")
endif()
if(DEFINED EXPECTED_OUTPUT AND NOT output MATCHES "${EXPECTED_OUTPUT}")
    message(FATAL_ERROR "Output does not match '${EXPECTED_OUTPUT}':\n${output}")
endif()
if(DEFINED EXPECTED_ERROR AND NOT errors MATCHES "${EXPECTED_ERROR}")
    message(FATAL_ERROR "Errors do not match '${EXPECTED_ERROR}':\n${errors}")
endif()
