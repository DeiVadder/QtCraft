# Tests are grouped by topic in subfolders (E-089); the shared fixtures stay in this folder.
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

qc_add_test(test_coordinates SOURCES world/test_coordinates.cpp LIBRARIES qc_common)
qc_add_test(test_texture_synthesizer SOURCES client/test_texture_synthesizer.cpp LIBRARIES qc_clientcore)
qc_add_test(test_camera SOURCES client/test_camera.cpp LIBRARIES qc_clientcore)
qc_add_test(test_mouse_capture SOURCES client/test_mouse_capture.cpp LIBRARIES qc_clientcore)
qc_add_test(test_sky SOURCES client/test_sky.cpp LIBRARIES qc_clientcore)
qc_add_test(test_ui_palette SOURCES client/test_ui_palette.cpp LIBRARIES qc_clientcore)
target_compile_definitions(test_ui_palette PRIVATE QTCRAFT_QML_DIR="${PROJECT_SOURCE_DIR}/src/app/qml"
                                                  QTCRAFT_THEME_HEADER="${PROJECT_SOURCE_DIR}/src/app/models/QcUiTheme.h")
qc_add_test(test_screen_choice SOURCES client/test_screen_choice.cpp LIBRARIES qc_clientcore)
qc_add_test(test_render_culling SOURCES client/test_render_culling.cpp LIBRARIES qc_clientcore)
qc_add_test(test_label_projector SOURCES client/test_label_projector.cpp QcTestWorld.h LIBRARIES qc_clientcore)
qc_add_test(test_app_icon SOURCES client/test_app_icon.cpp LIBRARIES qc_server qc_clientcore)
qc_add_test(test_content SOURCES content/test_content.cpp LIBRARIES qc_common)
qc_add_test(test_chunk SOURCES world/test_chunk.cpp LIBRARIES qc_common)
qc_add_test(test_protocol SOURCES gameplay/test_protocol.cpp LIBRARIES qc_common)
qc_add_test(test_physics SOURCES world/test_physics.cpp QcTestWorld.h LIBRARIES qc_common)
qc_add_test(test_mesher SOURCES client/test_mesher.cpp LIBRARIES qc_clientcore)
qc_add_test(test_client_server SOURCES gameplay/test_client_server.cpp LIBRARIES qc_server qc_clientcore)
qc_add_test(test_content_pack SOURCES content/test_content_pack.cpp LIBRARIES qc_server qc_clientcore)
qc_add_test(test_scenarios SOURCES gameplay/test_scenarios.cpp LIBRARIES qc_server qc_clientcore TIMEOUT 180)

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

# Command line errors of the game itself end it before a window opens.
add_test(NAME app_unknown_world_type_fails
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft>;--world-type;spiral;--screen;builtin;--background;--no-audio"
        -DEXPECTED_EXIT_CODE=1
        "-DEXPECTED_ERROR=Unknown world type 'spiral'"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
add_test(NAME app_flat_layers_need_flat_world_fails
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft>;--world-type;void;--flat-layers;bedrock;--screen;builtin;--background;--no-audio"
        -DEXPECTED_EXIT_CODE=1
        "-DEXPECTED_ERROR=--flat-layers needs --world-type flat"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
set_tests_properties(app_unknown_world_type_fails app_flat_layers_need_flat_world_fails PROPERTIES TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data")

qc_add_test(test_headless_cli SOURCES gameplay/test_headless_cli.cpp TIMEOUT 120)
target_compile_definitions(test_headless_cli PRIVATE QTCRAFT_HEADLESS_PATH="$<TARGET_FILE:qtcraft-headless>"
                                                    QTCRAFT_MODS_SOURCE_DIR="${PROJECT_SOURCE_DIR}/mods")
add_dependencies(test_headless_cli qtcraft-headless)
qc_add_test(test_reconciliation SOURCES client/test_reconciliation.cpp QcTestWorld.h LIBRARIES qc_clientcore)
qc_add_test(test_mesh_scheduler SOURCES client/test_mesh_scheduler.cpp LIBRARIES qc_clientcore)
add_test(NAME scenario_headless_failure_then_quit
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft-headless>;--script;${CMAKE_CURRENT_SOURCE_DIR}/scenarios/failure-then-quit.jsonl;--time-limit;60"
        -DEXPECTED_EXIT_CODE=1
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
set_tests_properties(scenario_headless_failure_then_quit PROPERTIES TIMEOUT 90
    ENVIRONMENT "QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data")
qc_add_test(test_noise SOURCES world/test_noise.cpp LIBRARIES qc_server)
qc_add_test(test_plant_growth SOURCES world/test_plant_growth.cpp LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_biome_distribution SOURCES world/test_biome_distribution.cpp LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_spawn_ground SOURCES world/test_spawn_ground.cpp QcRawClient.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_terrain_generator SOURCES world/test_terrain_generator.cpp LIBRARIES qc_server TIMEOUT 600)
qc_add_test(test_world_storage SOURCES world/test_world_storage.cpp LIBRARIES qc_server)
qc_add_test(test_persistence SOURCES world/test_persistence.cpp LIBRARIES qc_server qc_clientcore TIMEOUT 180)
qc_add_test(test_interaction SOURCES world/test_interaction.cpp QcTestWorld.h LIBRARIES qc_common)
qc_add_test(test_world_library SOURCES world/test_world_library.cpp LIBRARIES qc_server)
qc_add_test(test_structures SOURCES world/test_structures.cpp LIBRARIES qc_server)
qc_add_test(test_showcase SOURCES world/test_showcase.cpp LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_world_types SOURCES world/test_world_types.cpp QcRawClient.h LIBRARIES qc_server TIMEOUT 120)
qc_add_test(test_showcase_world SOURCES world/test_showcase_world.cpp QcRawClient.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_server_validation SOURCES gameplay/test_server_validation.cpp QcServerTestFixture.h QcRawClient.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_server_crafting SOURCES gameplay/test_server_crafting.cpp QcServerTestFixture.h QcRawClient.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_quick_move SOURCES gameplay/test_quick_move.cpp QcServerTestFixture.h QcRawClient.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_server_items_and_saves SOURCES gameplay/test_server_items_and_saves.cpp QcServerTestFixture.h QcRawClient.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_server_limits SOURCES gameplay/test_server_limits.cpp QcServerTestFixture.h QcRawClient.h QcTestWorld.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_mobs SOURCES gameplay/test_mobs.cpp LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_survival_scenario SOURCES gameplay/test_survival_scenario.cpp LIBRARIES qc_server qc_clientcore TIMEOUT 300)
qc_add_test(test_crafting SOURCES content/test_crafting.cpp LIBRARIES qc_common)
qc_add_test(test_mods SOURCES content/test_mods.cpp LIBRARIES qc_server)
target_compile_definitions(test_mods PRIVATE QTCRAFT_MODS_SOURCE_DIR="${PROJECT_SOURCE_DIR}/mods")
qc_add_test(test_bundled_mods SOURCES content/test_bundled_mods.cpp LIBRARIES qc_server)
target_compile_definitions(test_bundled_mods PRIVATE QTCRAFT_MODS_SOURCE_DIR="${PROJECT_SOURCE_DIR}/mods")
qc_add_test(test_settings SOURCES client/test_settings.cpp LIBRARIES qc_clientcore)
qc_add_test(test_items SOURCES gameplay/test_items.cpp QcTestWorld.h LIBRARIES qc_server)
qc_add_test(test_character SOURCES client/test_character.cpp QcTestWorld.h LIBRARIES qc_clientcore)
qc_add_test(test_machines SOURCES gameplay/test_machines.cpp QcTestWorld.h LIBRARIES qc_server)
qc_add_test(test_hunger SOURCES gameplay/test_hunger.cpp LIBRARIES qc_server)
qc_add_test(test_tick_scheduler SOURCES gameplay/test_tick_scheduler.cpp LIBRARIES qc_server)
qc_add_test(test_sound_content SOURCES content/test_sound_content.cpp LIBRARIES qc_server)
target_compile_definitions(test_sound_content PRIVATE QTCRAFT_MODS_SOURCE_DIR="${PROJECT_SOURCE_DIR}/mods")
qc_add_test(test_sound_synthesis SOURCES sound/test_sound_synthesis.cpp LIBRARIES qc_clientcore)
qc_add_test(test_audio_mixer SOURCES sound/test_audio_mixer.cpp LIBRARIES qc_clientcore)
qc_add_test(test_sound_world SOURCES sound/test_sound_world.cpp QcTestWorld.h LIBRARIES qc_clientcore)
qc_add_test(test_sound_events SOURCES sound/test_sound_events.cpp LIBRARIES qc_server TIMEOUT 180)
# Opens the real audio device when there is one (muted, well under a second of sound) and passes without.
qc_add_test(test_audio_output SOURCES sound/test_audio_output.cpp LIBRARIES qc_audio qc_server TIMEOUT 60)
qc_add_test(test_shapes SOURCES content/test_shapes.cpp QcTestWorld.h LIBRARIES qc_common)
qc_add_test(test_block_light SOURCES world/test_block_light.cpp QcTestWorld.h LIBRARIES qc_common)
qc_add_test(test_mesher_shapes SOURCES client/test_mesher_shapes.cpp LIBRARIES qc_clientcore)
qc_add_test(test_block_light_engine SOURCES client/test_block_light_engine.cpp LIBRARIES qc_clientcore)
qc_add_test(test_building SOURCES gameplay/test_building.cpp LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_liquids SOURCES world/test_liquids.cpp QcTestWorld.h LIBRARIES qc_server)
qc_add_test(test_world_details SOURCES world/test_world_details.cpp QcTestWorld.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_new_blocks SOURCES world/test_new_blocks.cpp QcServerTestFixture.h QcRawClient.h QcTestWorld.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_handbook SOURCES content/test_handbook.cpp LIBRARIES qc_server)
target_compile_definitions(test_handbook PRIVATE QTCRAFT_MODS_SOURCE_DIR="${PROJECT_SOURCE_DIR}/mods"
                                                QTCRAFT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
qc_add_test(test_key_bindings SOURCES client/test_key_bindings.cpp LIBRARIES qc_clientcore)
