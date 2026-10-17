# Scripted runs of qtcraft-headless (tests/scenarios/*.jsonl).
# Included by tests/CMakeLists.txt (decision E-417): paths are relative to tests/, and the test
# properties at the end of that file (labels, costs, required files) apply to these tests.

add_test(NAME scenario_headless_walk_and_jump
    COMMAND qtcraft-headless --script ${CMAKE_CURRENT_SOURCE_DIR}/scenarios/walk-and-jump.jsonl --time-limit 60)
add_test(NAME scenario_headless_unknown_command_fails
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft-headless>;--script;${CMAKE_CURRENT_SOURCE_DIR}/scenarios/unknown-command.jsonl;--time-limit;60"
        -DEXPECTED_EXIT_CODE=1
        "-DEXPECTED_OUTPUT=Unknown command 'teleport'"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
foreach(test scenario_headless_walk_and_jump scenario_headless_unknown_command_fails)
    set_tests_properties(${test} PROPERTIES TIMEOUT 90 ENVIRONMENT "QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data/${test}")
endforeach()
add_test(NAME scenario_headless_failure_then_quit
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft-headless>;--script;${CMAKE_CURRENT_SOURCE_DIR}/scenarios/failure-then-quit.jsonl;--time-limit;60"
        -DEXPECTED_EXIT_CODE=1
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
set_tests_properties(scenario_headless_failure_then_quit PROPERTIES TIMEOUT 90
    ENVIRONMENT "QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data/scenario_headless_failure_then_quit")
