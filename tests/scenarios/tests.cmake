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
# Virtual gamepads through the control interface (decision E-540).
add_test(NAME scenario_headless_gamepad_virtual
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft-headless>;--script;${CMAKE_CURRENT_SOURCE_DIR}/scenarios/gamepad-virtual.jsonl;--time-limit;60"
        -DEXPECTED_EXIT_CODE=0
        "-DEXPECTED_OUTPUT=\"id\":\"list\"[^\n]*\"leftX\":0\\.5,\"leftY\":-1,[^\n]*\"pressed\":\\[\"south\"\\]"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
add_test(NAME scenario_headless_gamepad_unknown_button
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft-headless>;--script;${CMAKE_CURRENT_SOURCE_DIR}/scenarios/gamepad-unknown-button.jsonl;--time-limit;60"
        -DEXPECTED_EXIT_CODE=1
        "-DEXPECTED_OUTPUT=Button 'jump' unknown"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
foreach(test scenario_headless_gamepad_virtual scenario_headless_gamepad_unknown_button)
    set_tests_properties(${test} PROPERTIES TIMEOUT 90 ENVIRONMENT "QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data/${test}")
endforeach()
# The admin channel through the control interface (decision E-848).
add_test(NAME scenario_headless_admin_channel
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft-headless>;--script;${CMAKE_CURRENT_SOURCE_DIR}/scenarios/admin-channel.jsonl;--time-limit;60"
        -DEXPECTED_EXIT_CODE=0
        "-DEXPECTED_OUTPUT=\"text\":\"\\[Server\\] hello from the admin\"[^\n]*\"id\":\"status\""
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
set_tests_properties(scenario_headless_admin_channel PROPERTIES TIMEOUT 90
    ENVIRONMENT "QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data/scenario_headless_admin_channel")
