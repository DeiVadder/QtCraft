qc_add_test(test_coordinates SOURCES test_coordinates.cpp LIBRARIES qc_common)
qc_add_test(test_texture_synthesizer SOURCES test_texture_synthesizer.cpp LIBRARIES qc_clientcore)
qc_add_test(test_camera SOURCES test_camera.cpp LIBRARIES qc_clientcore)
qc_add_test(test_content SOURCES test_content.cpp LIBRARIES qc_common)
qc_add_test(test_chunk SOURCES test_chunk.cpp LIBRARIES qc_common)
qc_add_test(test_protocol SOURCES test_protocol.cpp LIBRARIES qc_common)
qc_add_test(test_physics SOURCES test_physics.cpp QcTestWorld.h LIBRARIES qc_common)
qc_add_test(test_mesher SOURCES test_mesher.cpp LIBRARIES qc_clientcore)
qc_add_test(test_client_server SOURCES test_client_server.cpp LIBRARIES qc_server qc_clientcore)
qc_add_test(test_content_pack SOURCES test_content_pack.cpp LIBRARIES qc_server qc_clientcore)
qc_add_test(test_scenarios SOURCES test_scenarios.cpp LIBRARIES qc_server qc_clientcore TIMEOUT 180)

add_test(NAME scenario_headless_walk_and_jump
    COMMAND qtcraft-headless --script ${CMAKE_CURRENT_SOURCE_DIR}/scenarios/walk-and-jump.jsonl --time-limit 60)
add_test(NAME scenario_headless_unknown_command_fails
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft-headless>;--script;${CMAKE_CURRENT_SOURCE_DIR}/scenarios/unknown-command.jsonl;--time-limit;60"
        -DEXPECTED_EXIT_CODE=1
        "-DEXPECTED_OUTPUT=Unknown command 'teleport'"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
set_tests_properties(scenario_headless_walk_and_jump scenario_headless_unknown_command_fails PROPERTIES TIMEOUT 90)

qc_add_test(test_headless_cli SOURCES test_headless_cli.cpp TIMEOUT 120)
target_compile_definitions(test_headless_cli PRIVATE QTCRAFT_HEADLESS_PATH="$<TARGET_FILE:qtcraft-headless>")
add_dependencies(test_headless_cli qtcraft-headless)
qc_add_test(test_reconciliation SOURCES test_reconciliation.cpp QcTestWorld.h LIBRARIES qc_clientcore)
qc_add_test(test_mesh_scheduler SOURCES test_mesh_scheduler.cpp LIBRARIES qc_clientcore)
