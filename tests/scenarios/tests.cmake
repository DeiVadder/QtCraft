# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Scripted runs of qtcraft-headless (tests/scenarios/*.jsonl), the C++ scenario tests and measurements.
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
qc_add_test(test_client_server SOURCES scenarios/test_client_server.cpp LIBRARIES qc_server qc_worlds qc_clientcore TIMEOUT 180)
qc_add_test(test_scenarios SOURCES scenarios/test_scenarios.cpp LIBRARIES qc_server qc_worlds qc_clientcore TIMEOUT 750)
# A virtual gamepad steering the player through the control interface (decision E-541); the client
# ticks by hand, only the flight case and the waits for the server run in real time (label realtime).
qc_add_test(test_gamepad_scenarios SOURCES scenarios/test_gamepad_scenarios.cpp LIBRARIES qc_server qc_worlds qc_clientcore)
qc_add_test(test_headless_cli SOURCES scenarios/test_headless_cli.cpp TIMEOUT 800)
target_compile_definitions(test_headless_cli PRIVATE QTCRAFT_HEADLESS_PATH="$<TARGET_FILE:qtcraft-headless>"
                                                    QTCRAFT_MODS_SOURCE_DIR="${PROJECT_SOURCE_DIR}/mods")
add_dependencies(test_headless_cli qtcraft-headless)
qc_add_test(test_survival_scenario SOURCES scenarios/test_survival_scenario.cpp LIBRARIES qc_server qc_worlds qc_clientcore TIMEOUT 300)
qc_add_test(test_explore_measure SOURCES scenarios/test_explore_measure.cpp fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 600)
# The server side of a benchmark scenario without a window (scripts/bench-headless.sh, decision E-388).
qc_add_test(test_scenario_measure SOURCES scenarios/test_scenario_measure.cpp fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 600)
