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
set_tests_properties(scenario_headless_walk_and_jump scenario_headless_unknown_command_fails PROPERTIES TIMEOUT 90
    ENVIRONMENT "QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data")

qc_add_test(test_headless_cli SOURCES test_headless_cli.cpp TIMEOUT 120)
target_compile_definitions(test_headless_cli PRIVATE QTCRAFT_HEADLESS_PATH="$<TARGET_FILE:qtcraft-headless>"
                                                    QTCRAFT_MODS_SOURCE_DIR="${PROJECT_SOURCE_DIR}/mods")
add_dependencies(test_headless_cli qtcraft-headless)
qc_add_test(test_reconciliation SOURCES test_reconciliation.cpp QcTestWorld.h LIBRARIES qc_clientcore)
qc_add_test(test_mesh_scheduler SOURCES test_mesh_scheduler.cpp LIBRARIES qc_clientcore)
add_test(NAME scenario_headless_failure_then_quit
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft-headless>;--script;${CMAKE_CURRENT_SOURCE_DIR}/scenarios/failure-then-quit.jsonl;--time-limit;60"
        -DEXPECTED_EXIT_CODE=1
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
set_tests_properties(scenario_headless_failure_then_quit PROPERTIES TIMEOUT 90
    ENVIRONMENT "QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data")
qc_add_test(test_noise SOURCES test_noise.cpp LIBRARIES qc_server)
qc_add_test(test_terrain_generator SOURCES test_terrain_generator.cpp LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_world_storage SOURCES test_world_storage.cpp LIBRARIES qc_server)
qc_add_test(test_persistence SOURCES test_persistence.cpp LIBRARIES qc_server qc_clientcore TIMEOUT 180)
qc_add_test(test_interaction SOURCES test_interaction.cpp QcTestWorld.h LIBRARIES qc_common)
qc_add_test(test_world_library SOURCES test_world_library.cpp LIBRARIES qc_server)
qc_add_test(test_server_validation SOURCES test_server_validation.cpp LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_mobs SOURCES test_mobs.cpp LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_survival_scenario SOURCES test_survival_scenario.cpp LIBRARIES qc_server qc_clientcore TIMEOUT 300)
qc_add_test(test_crafting SOURCES test_crafting.cpp LIBRARIES qc_common)
qc_add_test(test_mods SOURCES test_mods.cpp LIBRARIES qc_server)
target_compile_definitions(test_mods PRIVATE QTCRAFT_MODS_SOURCE_DIR="${PROJECT_SOURCE_DIR}/mods")
qc_add_test(test_settings SOURCES test_settings.cpp LIBRARIES qc_clientcore)
