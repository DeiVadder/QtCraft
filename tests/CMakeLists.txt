# Tests are grouped by topic in subfolders (E-089); the shared fixtures stay in this folder.
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

qc_add_test(test_coordinates SOURCES world/test_coordinates.cpp LIBRARIES qc_common)
qc_add_test(test_texture_synthesizer SOURCES client/test_texture_synthesizer.cpp LIBRARIES qc_clientcore)
qc_add_test(test_texture_packs SOURCES client/test_texture_packs.cpp LIBRARIES qc_clientcore)
qc_add_test(test_texture_atlas SOURCES client/test_texture_atlas.cpp LIBRARIES qc_server qc_clientcore Qt::Concurrent)
qc_add_test(test_atlas_fill SOURCES client/test_atlas_fill.cpp LIBRARIES qc_server qc_clientcore)
# The settings' texture pack controller at model level (decision E-301), built from the app's sources.
qc_add_test(test_texture_pack_controller SOURCES client/test_texture_pack_controller.cpp
            ${PROJECT_SOURCE_DIR}/src/app/controllers/QcTexturePackController.cpp
            ${PROJECT_SOURCE_DIR}/src/app/models/QcTexturePackPreviewProvider.cpp
            LIBRARIES qc_clientcore Qt::Concurrent Qt::Quick)
target_include_directories(test_texture_pack_controller PRIVATE ${PROJECT_SOURCE_DIR}/src/app/controllers ${PROJECT_SOURCE_DIR}/src/app/models)
# The hitch window of a texture pack switch in a benchmark run (decision E-332), from the app's sources.
qc_add_test(test_pack_switch_hitch SOURCES client/test_pack_switch_hitch.cpp ${PROJECT_SOURCE_DIR}/src/app/hooks/QcPackSwitchHitch.cpp
            LIBRARIES Qt::Core)
target_include_directories(test_pack_switch_hitch PRIVATE ${PROJECT_SOURCE_DIR}/src/app/hooks)
qc_add_test(test_camera SOURCES client/test_camera.cpp LIBRARIES qc_clientcore)
qc_add_test(test_mouse_capture SOURCES client/test_mouse_capture.cpp LIBRARIES qc_clientcore)
qc_add_test(test_sky SOURCES client/test_sky.cpp LIBRARIES qc_clientcore)
qc_add_test(test_ui_palette SOURCES client/test_ui_palette.cpp LIBRARIES qc_clientcore)
target_compile_definitions(test_ui_palette PRIVATE QTCRAFT_QML_DIR="${PROJECT_SOURCE_DIR}/src/app/qml"
                                                  QTCRAFT_THEME_HEADER="${PROJECT_SOURCE_DIR}/src/app/models/QcUiTheme.h")
qc_add_test(test_screen_choice SOURCES client/test_screen_choice.cpp LIBRARIES qc_clientcore)
qc_add_test(test_render_culling SOURCES client/test_render_culling.cpp LIBRARIES qc_clientcore)
qc_add_test(test_label_projector SOURCES client/test_label_projector.cpp QcTestWorld.h LIBRARIES qc_clientcore)
qc_add_test(test_debug_report SOURCES client/test_debug_report.cpp QcTestWorld.h LIBRARIES qc_clientcore)
qc_add_test(test_app_icon SOURCES client/test_app_icon.cpp LIBRARIES qc_server qc_clientcore)
qc_add_test(test_web_handbook SOURCES client/test_web_handbook.cpp LIBRARIES qc_server qc_clientcore Qt::Qml)
target_compile_definitions(test_web_handbook PRIVATE QTCRAFT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
qc_add_test(test_content SOURCES content/test_content.cpp LIBRARIES qc_common)
qc_add_test(test_chunk SOURCES world/test_chunk.cpp LIBRARIES qc_common)
qc_add_test(test_protocol SOURCES gameplay/test_protocol.cpp LIBRARIES qc_common)
qc_add_test(test_entity_protocol SOURCES gameplay/test_entity_protocol.cpp LIBRARIES qc_common)
qc_add_test(test_game_mode_protocol SOURCES gameplay/test_game_mode_protocol.cpp LIBRARIES qc_common)
qc_add_test(test_physics SOURCES world/test_physics.cpp QcTestWorld.h LIBRARIES qc_common)
qc_add_test(test_flight SOURCES world/test_flight.cpp QcTestWorld.h LIBRARIES qc_common)
qc_add_test(test_mesher SOURCES client/test_mesher.cpp LIBRARIES qc_clientcore)
qc_add_test(test_client_server SOURCES gameplay/test_client_server.cpp LIBRARIES qc_server qc_clientcore)
qc_add_test(test_content_pack SOURCES content/test_content_pack.cpp LIBRARIES qc_server qc_clientcore)
qc_add_test(test_creative_catalog SOURCES content/test_creative_catalog.cpp LIBRARIES qc_server)
qc_add_test(test_scenarios SOURCES gameplay/test_scenarios.cpp LIBRARIES qc_server qc_clientcore TIMEOUT 180)

# Argument checks of the A/B benchmark script (settings per label, decision E-332); runs no game.
add_test(NAME script_bench_ab_args
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_bench_ab_args.sh ${PROJECT_SOURCE_DIR}/scripts/bench-ab.sh)
set_tests_properties(script_bench_ab_args PROPERTIES TIMEOUT 60)

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

# Offscreen rendering (E-229): the platform without windows shows the main menu and the error screen
# without crashing, and a scripted run draws the world without a shown window.
add_test(NAME app_offscreen_platform_menu_screenshot
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft>;--no-audio"
        -DEXPECTED_EXIT_CODE=0
        "-DEXPECTED_ERROR=Rendering offscreen with Metal"
        -DEXPECTED_FILE=${CMAKE_CURRENT_BINARY_DIR}/offscreen-menu.png
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
add_test(NAME app_offscreen_platform_error_screen_fails
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft>;--world-type;flat;--flat-layers;bedrock,3*nonsense;--no-audio"
        -DEXPECTED_EXIT_CODE=1
        "-DEXPECTED_ERROR=Invalid layer '3\\*nonsense'"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
set_tests_properties(app_offscreen_platform_menu_screenshot app_offscreen_platform_error_screen_fails PROPERTIES TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data;QTCRAFT_SCREENSHOT=${CMAKE_CURRENT_BINARY_DIR}/offscreen-menu.png;QTCRAFT_SCREENSHOT_FRAMES=10")
# A picture of the world is far larger than one of the bare HUD over the background (about 50 kB).
add_test(NAME app_offscreen_world_screenshot
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft>;--play;--seed;42;--look;45,-20;--view-distance;4;--no-audio"
        -DEXPECTED_EXIT_CODE=0
        -DEXPECTED_FILE=${CMAKE_CURRENT_BINARY_DIR}/offscreen-world.png
        -DEXPECTED_MIN_SIZE=200000
        "-DFORBIDDEN_ERROR=Cannot open shader|Cannot create graphics pipeline|Cannot create entity pipeline"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
set_tests_properties(app_offscreen_world_screenshot PROPERTIES TIMEOUT 120
    ENVIRONMENT "QTCRAFT_OFFSCREEN=1;QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data;QTCRAFT_SCREENSHOT=${CMAKE_CURRENT_BINARY_DIR}/offscreen-world.png;QTCRAFT_SCREENSHOT_FRAMES=30")

# The full debug display with every visualisation and the debug options page load without QML
# errors, binding loops or other warnings of the QML files (decision E-225).
add_test(NAME app_offscreen_debug_display_qml
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft>;--play;--seed;42;--view-distance;2;--debug-display;full;--debug-show;chunkBorders,hitboxes,lightValues,mobGoals,remeshFlash;--overlay;debug;--no-audio"
        -DEXPECTED_EXIT_CODE=0
        -DEXPECTED_FILE=${CMAKE_CURRENT_BINARY_DIR}/offscreen-debug.png
        "-DFORBIDDEN_ERROR=qrc:[^\n]*(Error|Binding loop|Unable to assign|is not a type|Cannot read)"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
set_tests_properties(app_offscreen_debug_display_qml PROPERTIES TIMEOUT 120
    ENVIRONMENT "QTCRAFT_OFFSCREEN=1;QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data-debug;QTCRAFT_SCREENSHOT=${CMAKE_CURRENT_BINARY_DIR}/offscreen-debug.png;QTCRAFT_SCREENSHOT_FRAMES=60")
# The character previews (decision E-231): the main menu's profile card, the character screen with
# its presets and the inventory load without QML errors or binding loops, with the --preview hook,
# and the previews of the character screen and the inventory draw the figure.
add_test(NAME app_offscreen_character_preview_qml
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft>;--play;--seed;42;--view-distance;2;--overlay;character;--overlay;@1500 inventory;--preview;animation=mine,side=back,gaze=0.5:0.5,zoom=0.3;--no-audio"
        -DEXPECTED_EXIT_CODE=0
        "-DEXPECTED_ERROR=characterPreview: first figure with [1-9][0-9]+ vertices.*inventoryPreview: first figure with [1-9][0-9]+ vertices"
        -DEXPECTED_FILE=${CMAKE_CURRENT_BINARY_DIR}/offscreen-preview.png
        "-DFORBIDDEN_ERROR=qrc:[^\n]*(Error|Binding loop|Unable to assign|is not a type|Cannot read|Unable to)"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
set_tests_properties(app_offscreen_character_preview_qml PROPERTIES TIMEOUT 120
    ENVIRONMENT "QTCRAFT_OFFSCREEN=1;QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data-preview;QTCRAFT_SCREENSHOT=${CMAKE_CURRENT_BINARY_DIR}/offscreen-preview.png;QTCRAFT_SCREENSHOT_FRAMES=60")
# Armour (decisions E-234, E-238): the inventory with the armour slots, the HUD's armour bar and an
# armour stand with a name load without QML errors, and the inventory's preview draws the armour.
add_test(NAME app_offscreen_armour_qml
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft>;--play;--world-type;flat;--view-distance;2;--chat;@200 /equip diamond;--chat;@300 /summon armor_stand equip=iron name=Tin showname;--overlay;@1500 inventory;--no-audio"
        -DEXPECTED_EXIT_CODE=0
        "-DEXPECTED_ERROR=inventoryPreview: first figure with [1-9][0-9]+ vertices"
        -DEXPECTED_FILE=${CMAKE_CURRENT_BINARY_DIR}/offscreen-armour.png
        "-DFORBIDDEN_ERROR=qrc:[^\n]*(Error|Binding loop|Unable to assign|is not a type|Cannot read|Unable to)"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
set_tests_properties(app_offscreen_armour_qml PROPERTIES TIMEOUT 120
    ENVIRONMENT "QTCRAFT_OFFSCREEN=1;QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data-armour;QTCRAFT_SCREENSHOT=${CMAKE_CURRENT_BINARY_DIR}/offscreen-armour.png;QTCRAFT_SCREENSHOT_FRAMES=90")
add_test(NAME app_preview_hook_refused
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft>;--preview;animation=dance;--screen;builtin;--background;--no-audio"
        -DEXPECTED_EXIT_CODE=1
        "-DEXPECTED_ERROR=Invalid --preview setting 'animation=dance'"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
set_tests_properties(app_preview_hook_refused PROPERTIES TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data")
# Offscreen runs are screenshot runs: a benchmark (frame times of the real window) and a run
# without a screenshot (nothing would end it) are refused at once.
add_test(NAME app_offscreen_benchmark_refused
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft>;--play;--no-audio"
        -DEXPECTED_EXIT_CODE=1
        "-DEXPECTED_ERROR=Benchmarks measure the real window"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
set_tests_properties(app_offscreen_benchmark_refused PROPERTIES TIMEOUT 60
    ENVIRONMENT "QTCRAFT_OFFSCREEN=1;QTCRAFT_BENCHMARK=5;QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data;QTCRAFT_SCREENSHOT=${CMAKE_CURRENT_BINARY_DIR}/offscreen-refused.png")
add_test(NAME app_offscreen_without_screenshot_refused
    COMMAND ${CMAKE_COMMAND} -E env --unset=QTCRAFT_SCREENSHOT --unset=QTCRAFT_BENCHMARK ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft>;--play;--no-audio"
        -DEXPECTED_EXIT_CODE=1
        "-DEXPECTED_ERROR=An offscreen run needs QTCRAFT_SCREENSHOT"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
set_tests_properties(app_offscreen_without_screenshot_refused PROPERTIES TIMEOUT 60
    ENVIRONMENT "QTCRAFT_OFFSCREEN=1;QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data")

qc_add_test(test_headless_cli SOURCES gameplay/test_headless_cli.cpp TIMEOUT 240)
target_compile_definitions(test_headless_cli PRIVATE QTCRAFT_HEADLESS_PATH="$<TARGET_FILE:qtcraft-headless>"
                                                    QTCRAFT_MODS_SOURCE_DIR="${PROJECT_SOURCE_DIR}/mods")
add_dependencies(test_headless_cli qtcraft-headless)
qc_add_test(test_reconciliation SOURCES client/test_reconciliation.cpp QcTestWorld.h LIBRARIES qc_clientcore)
qc_add_test(test_interpolation SOURCES client/test_interpolation.cpp QcLatencyTransport.h LIBRARIES qc_server qc_clientcore)
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
qc_add_test(test_farming SOURCES world/test_farming.cpp LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_biome_distribution SOURCES world/test_biome_distribution.cpp LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_spawn_ground SOURCES world/test_spawn_ground.cpp QcRawClient.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_terrain_generator SOURCES world/test_terrain_generator.cpp LIBRARIES qc_server TIMEOUT 600)
qc_add_test(test_world_storage SOURCES world/test_world_storage.cpp LIBRARIES qc_server)
qc_add_test(test_persistence SOURCES world/test_persistence.cpp LIBRARIES qc_server qc_clientcore TIMEOUT 180)
qc_add_test(test_interaction SOURCES world/test_interaction.cpp QcTestWorld.h LIBRARIES qc_common)
qc_add_test(test_world_library SOURCES world/test_world_library.cpp LIBRARIES qc_server)
target_compile_definitions(test_world_library PRIVATE QTCRAFT_OLD_WORLD="${CMAKE_CURRENT_SOURCE_DIR}/data/world-before-31-1")
qc_add_test(test_structures SOURCES world/test_structures.cpp LIBRARIES qc_server)
qc_add_test(test_showcase SOURCES world/test_showcase.cpp LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_world_types SOURCES world/test_world_types.cpp QcRawClient.h LIBRARIES qc_server TIMEOUT 120)
qc_add_test(test_showcase_world SOURCES world/test_showcase_world.cpp QcRawClient.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_server_commands SOURCES gameplay/test_server_commands.cpp QcServerTestFixture.h QcRawClient.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_player_management SOURCES gameplay/test_player_management.cpp QcServerTestFixture.h QcRawClient.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_world_maintenance SOURCES gameplay/test_world_maintenance.cpp QcServerTestFixture.h QcRawClient.h LIBRARIES qc_server TIMEOUT 240)
qc_add_test(test_tick_control SOURCES gameplay/test_tick_control.cpp QcServerTestFixture.h QcRawClient.h LIBRARIES qc_server TIMEOUT 120)
qc_add_test(test_server_validation SOURCES gameplay/test_server_validation.cpp QcServerTestFixture.h QcRawClient.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_server_crafting SOURCES gameplay/test_server_crafting.cpp QcServerTestFixture.h QcRawClient.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_armour_server SOURCES gameplay/test_armour_server.cpp QcServerTestFixture.h QcRawClient.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_armour_stands SOURCES gameplay/test_armour_stands.cpp QcServerTestFixture.h QcRawClient.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_quick_move SOURCES gameplay/test_quick_move.cpp QcServerTestFixture.h QcRawClient.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_server_items_and_saves SOURCES gameplay/test_server_items_and_saves.cpp QcServerTestFixture.h QcRawClient.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_server_limits SOURCES gameplay/test_server_limits.cpp QcServerTestFixture.h QcRawClient.h QcTestWorld.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_mobs SOURCES gameplay/test_mobs.cpp LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_creature_summons SOURCES gameplay/test_creature_summons.cpp QcServerTestFixture.h QcRawClient.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_body_fit SOURCES gameplay/test_body_fit.cpp LIBRARIES qc_server TIMEOUT 60)
qc_add_test(test_survival_scenario SOURCES gameplay/test_survival_scenario.cpp LIBRARIES qc_server qc_clientcore TIMEOUT 300)
qc_add_test(test_crafting SOURCES content/test_crafting.cpp LIBRARIES qc_common)
qc_add_test(test_armour SOURCES content/test_armour.cpp LIBRARIES qc_server)
qc_add_test(test_mods SOURCES content/test_mods.cpp LIBRARIES qc_server)
target_compile_definitions(test_mods PRIVATE QTCRAFT_MODS_SOURCE_DIR="${PROJECT_SOURCE_DIR}/mods")
# Headless measurement of the translations (stage 43); QTCRAFT_TRANSLATION_MEASURE=full for the recorded sizes.
qc_add_test(test_translation_measure SOURCES content/test_translation_measure.cpp LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_translation_limits SOURCES content/test_translation_limits.cpp LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_bundled_mods SOURCES content/test_bundled_mods.cpp LIBRARIES qc_server)
target_compile_definitions(test_bundled_mods PRIVATE QTCRAFT_MODS_SOURCE_DIR="${PROJECT_SOURCE_DIR}/mods")
qc_add_test(test_settings SOURCES client/test_settings.cpp LIBRARIES qc_clientcore)
qc_add_test(test_items SOURCES gameplay/test_items.cpp QcTestWorld.h LIBRARIES qc_server)
qc_add_test(test_character SOURCES client/test_character.cpp QcTestWorld.h LIBRARIES qc_clientcore)
qc_add_test(test_armour_mesh SOURCES client/test_armour_mesh.cpp LIBRARIES qc_server qc_clientcore)
qc_add_test(test_character_preview SOURCES client/test_character_preview.cpp LIBRARIES qc_server qc_clientcore)
qc_add_test(test_cape SOURCES client/test_cape.cpp LIBRARIES qc_server qc_clientcore)
qc_add_test(test_cape_swing SOURCES client/test_cape_swing.cpp LIBRARIES qc_clientcore)
qc_add_test(test_machines SOURCES gameplay/test_machines.cpp QcTestWorld.h LIBRARIES qc_server)
qc_add_test(test_hunger SOURCES gameplay/test_hunger.cpp LIBRARIES qc_server)
qc_add_test(test_creative_mode SOURCES gameplay/test_creative_mode.cpp QcServerTestFixture.h QcRawClient.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_void_zone SOURCES gameplay/test_void_zone.cpp QcServerTestFixture.h QcRawClient.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_farming_server SOURCES gameplay/test_farming_server.cpp QcServerTestFixture.h QcRawClient.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_tick_scheduler SOURCES gameplay/test_tick_scheduler.cpp LIBRARIES qc_server)
qc_add_test(test_tick_phases SOURCES gameplay/test_tick_phases.cpp QcServerTestFixture.h QcRawClient.h LIBRARIES qc_server TIMEOUT 120)
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
# Signal Dust (decisions E-333 to E-341): wires, components, pistons and their quirks.
qc_add_test(test_signal_network SOURCES gameplay/test_signal_network.cpp QcTestWorld.h LIBRARIES qc_server)
qc_add_test(test_signal_circuits SOURCES gameplay/test_signal_circuits.cpp QcCircuitFixture.h QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 180)
# Headless measurement of the signal phase; QTCRAFT_SIGNAL_MEASURE=full for the recorded sizes.
qc_add_test(test_signal_measure SOURCES gameplay/test_signal_measure.cpp QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_signal_bodies SOURCES gameplay/test_signal_bodies.cpp QcCircuitFixture.h QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_signal_showcase SOURCES world/test_signal_showcase.cpp QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_signal_pistons SOURCES gameplay/test_signal_pistons.cpp QcCircuitFixture.h QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_signal_hoppers SOURCES gameplay/test_signal_hoppers.cpp QcMachineFixture.h QcCircuitFixture.h QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_block_entity_compare SOURCES gameplay/test_block_entity_compare.cpp QcMachineFixture.h QcCircuitFixture.h QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 600)
target_compile_definitions(test_block_entity_compare PRIVATE QTCRAFT_COMPARE_GOLDEN="${CMAKE_CURRENT_SOURCE_DIR}/data/block-entity-compare-before-31-1.txt")
qc_add_test(test_entity_compare SOURCES gameplay/test_entity_compare.cpp QcMachineFixture.h QcCircuitFixture.h QcServerTestFixture.h QcRawClient.h LIBRARIES qc_server TIMEOUT 600)
target_compile_definitions(test_entity_compare PRIVATE QTCRAFT_COMPARE_GOLDEN="${CMAKE_CURRENT_SOURCE_DIR}/data/entity-compare-before-31-2.txt")
qc_add_test(test_entity_model SOURCES gameplay/test_entity_model.cpp QcMachineFixture.h QcCircuitFixture.h QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_entity_measure SOURCES gameplay/test_entity_measure.cpp QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_block_entities SOURCES gameplay/test_block_entities.cpp QcMachineFixture.h QcCircuitFixture.h QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_block_entity_storage SOURCES world/test_block_entity_storage.cpp QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
target_compile_definitions(test_block_entity_storage PRIVATE QTCRAFT_OLD_WORLD="${CMAKE_CURRENT_SOURCE_DIR}/data/world-before-31-1")
qc_add_test(test_entity_storage SOURCES world/test_entity_storage.cpp LIBRARIES qc_server TIMEOUT 300)
target_compile_definitions(test_entity_storage PRIVATE QTCRAFT_OLD_WORLD="${CMAKE_CURRENT_SOURCE_DIR}/data/world-before-31-2")
qc_add_test(test_block_entity_measure SOURCES gameplay/test_block_entity_measure.cpp QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_simulation_limits SOURCES gameplay/test_simulation_limits.cpp QcMachineFixture.h QcCircuitFixture.h QcServerTestFixture.h LIBRARIES qc_server qc_network)
qc_add_test(test_signal_hopper_edges SOURCES gameplay/test_signal_hopper_edges.cpp QcMachineFixture.h QcCircuitFixture.h QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_signal_dispensers SOURCES gameplay/test_signal_dispensers.cpp QcMachineFixture.h QcCircuitFixture.h QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_signal_readers SOURCES gameplay/test_signal_readers.cpp QcMachineFixture.h QcCircuitFixture.h QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_world_details SOURCES world/test_world_details.cpp QcTestWorld.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_new_blocks SOURCES world/test_new_blocks.cpp QcServerTestFixture.h QcRawClient.h QcTestWorld.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_handbook SOURCES content/test_handbook.cpp LIBRARIES qc_server)
target_compile_definitions(test_handbook PRIVATE QTCRAFT_MODS_SOURCE_DIR="${PROJECT_SOURCE_DIR}/mods"
                                                QTCRAFT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
qc_add_test(test_server_messages SOURCES content/test_server_messages.cpp LIBRARIES Qt::Core)
target_compile_definitions(test_server_messages PRIVATE QTCRAFT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
qc_add_test(test_key_bindings SOURCES client/test_key_bindings.cpp LIBRARIES qc_clientcore)
# Field of view while sprinting and the sprint latch (decision E-280).
qc_add_test(test_sprint_feel SOURCES client/test_sprint_feel.cpp LIBRARIES qc_clientcore)
# The control palette of the windows (decision E-279).
qc_add_test(test_control_palette SOURCES client/test_control_palette.cpp ${PROJECT_SOURCE_DIR}/src/app/models/QcControlPalette.cpp
            LIBRARIES qc_clientcore Qt::Quick Qt::Qml)
target_include_directories(test_control_palette PRIVATE ${PROJECT_SOURCE_DIR}/src/app/models)
# The graph of the debug display lives in the app; its scene graph nodes need no window.
qc_add_test(test_graph_item SOURCES client/test_graph_item.cpp ${PROJECT_SOURCE_DIR}/src/app/rendering/QcGraphItem.cpp
            LIBRARIES Qt::Quick)
target_include_directories(test_graph_item PRIVATE ${PROJECT_SOURCE_DIR}/src/app/rendering)
# The baked shader variants of the game (decision E-332): read from the game's build tree.
qc_add_test(test_shader_variants SOURCES client/test_shader_variants.cpp LIBRARIES Qt::Gui Qt::GuiPrivate)
target_compile_definitions(test_shader_variants PRIVATE QTCRAFT_QSB_DIR="${PROJECT_BINARY_DIR}/src/app/.qsb/shaders")
add_dependencies(test_shader_variants qtcraft)
# The shared atlas texture of the character previews, on the Null backend of QRhi.
qc_add_test(test_preview_atlas_cache SOURCES client/test_preview_atlas_cache.cpp ${PROJECT_SOURCE_DIR}/src/app/rendering/QcPreviewAtlasCache.cpp
            ${PROJECT_SOURCE_DIR}/src/app/rendering/QcAtlasTexture.cpp LIBRARIES qc_clientcore Qt::Gui Qt::GuiPrivate)
target_include_directories(test_preview_atlas_cache PRIVATE ${PROJECT_SOURCE_DIR}/src/app/rendering)

# QML load test of the character screen in the real game, offscreen (decision E-240). Not under
# sanitizers: the whole game with its render thread is too slow there for the time limit.
if(NOT QC_SANITIZERS)
    add_test(NAME test_character_screen_qml COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_character_screen_qml.sh $<TARGET_FILE:qtcraft>)
    set_tests_properties(test_character_screen_qml PROPERTIES TIMEOUT 120)
    # The main menu and its screens fit the smallest window (1024 x 640): nothing shown lies
    # outside it, the Quit button included.
    add_test(NAME app_offscreen_menu_layout COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_menu_layout.sh $<TARGET_FILE:qtcraft>)
    set_tests_properties(app_offscreen_menu_layout PROPERTIES TIMEOUT 240)
endif()

# Multiplayer (stage 25, decisions E-251 to E-256).
qc_add_test(test_login_protocol SOURCES network/test_login_protocol.cpp LIBRARIES qc_network Qt::Network)
qc_add_test(test_server_login SOURCES network/test_server_login.cpp QcServerTestFixture.h QcRawClient.h QcTestLogin.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_tcp_transport SOURCES network/test_tcp_transport.cpp LIBRARIES qc_network Qt::Network)
qc_add_test(test_network_game SOURCES network/test_network_game.cpp QcServerTestFixture.h QcRawClient.h QcTestLogin.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_dedicated_server SOURCES network/test_dedicated_server.cpp QcRawClient.h QcTestLogin.h LIBRARIES qc_server TIMEOUT 240)
target_compile_definitions(test_dedicated_server PRIVATE QTCRAFT_HEADLESS_PATH="$<TARGET_FILE:qtcraft-headless>"
                                                         QTCRAFT_MODS_SOURCE_DIR="${PROJECT_SOURCE_DIR}/mods")
add_dependencies(test_dedicated_server qtcraft-headless)
qc_add_test(test_world_backup SOURCES gameplay/test_world_backup.cpp QcServerTestFixture.h QcRawClient.h QcTestLogin.h LIBRARIES qc_server)
qc_add_test(test_network_client SOURCES network/test_network_client.cpp LIBRARIES qc_server qc_clientcore)
qc_add_test(test_latency SOURCES network/test_latency.cpp QcLatencyTransport.h LIBRARIES qc_server qc_clientcore TIMEOUT 240)
qc_add_test(test_creative_client SOURCES network/test_creative_client.cpp QcLatencyTransport.h LIBRARIES qc_server qc_clientcore TIMEOUT 180)
qc_add_test(test_void_client SOURCES network/test_void_client.cpp QcLatencyTransport.h LIBRARIES qc_server qc_clientcore TIMEOUT 180)

# The multiplayer screens (stage 25b, decisions E-258 to E-261): recent servers, LAN addresses, join and leave
# lines and the settings, without a window.
qc_add_test(test_multiplayer_ui SOURCES client/test_multiplayer_ui.cpp
            ${PROJECT_SOURCE_DIR}/src/app/multiplayer/QcServerHistory.cpp
            ${PROJECT_SOURCE_DIR}/src/app/multiplayer/QcMultiplayerSettings.cpp
            ${PROJECT_SOURCE_DIR}/src/app/multiplayer/QcLanAddresses.cpp
            ${PROJECT_SOURCE_DIR}/src/app/multiplayer/QcPlayerList.cpp
            LIBRARIES qc_clientcore qc_network Qt::Qml)
target_include_directories(test_multiplayer_ui PRIVATE ${PROJECT_SOURCE_DIR}/src/app/multiplayer)
# The menu's multiplayer screen with a refused join (nobody listens on port 1), and a world opened
# to the network on this computer only with the open-to-LAN screen and the player list, load
# without QML errors or binding loops.
if(NOT QC_SANITIZERS)
    add_test(NAME app_offscreen_multiplayer_menu_qml
        COMMAND ${CMAKE_COMMAND}
            "-DCOMMAND=$<TARGET_FILE:qtcraft>;--overlay;multiplayer;--join;127.0.0.1:1;--player-name;Alex;--no-mods;--no-audio"
            -DEXPECTED_EXIT_CODE=0
            "-DEXPECTED_ERROR=Joining failed: Nobody accepts connections"
            -DEXPECTED_FILE=${CMAKE_CURRENT_BINARY_DIR}/offscreen-multiplayer.png
            "-DFORBIDDEN_ERROR=qrc:[^\n]*(Error|Binding loop|Unable to assign|is not a type|Cannot read|Unable to)"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
    set_tests_properties(app_offscreen_multiplayer_menu_qml PROPERTIES TIMEOUT 120
        ENVIRONMENT "QTCRAFT_OFFSCREEN=1;QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data-multiplayer;QTCRAFT_SCREENSHOT=${CMAKE_CURRENT_BINARY_DIR}/offscreen-multiplayer.png;QTCRAFT_SCREENSHOT_FRAMES=60")
    add_test(NAME app_offscreen_open_to_lan_qml
        COMMAND ${CMAKE_COMMAND}
            "-DCOMMAND=$<TARGET_FILE:qtcraft>;--play;--world-type;flat;--view-distance;2;--host-lan;0;--overlay;@1000 lan;--overlay;@2000 ;--overlay;@2500 players;--no-mods;--no-audio"
            -DEXPECTED_EXIT_CODE=0
            "-DEXPECTED_ERROR=Listening on 127.0.0.1 port"
            -DEXPECTED_FILE=${CMAKE_CURRENT_BINARY_DIR}/offscreen-lan.png
            "-DFORBIDDEN_ERROR=qrc:[^\n]*(Error|Binding loop|Unable to assign|is not a type|Cannot read|Unable to)"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
    set_tests_properties(app_offscreen_open_to_lan_qml PROPERTIES TIMEOUT 120
        ENVIRONMENT "QTCRAFT_OFFSCREEN=1;QTCRAFT_LAN_BIND=127.0.0.1;QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data-lan;QTCRAFT_SCREENSHOT=${CMAKE_CURRENT_BINARY_DIR}/offscreen-lan.png;QTCRAFT_SCREENSHOT_FRAMES=30")
endif()
# Bot clients in their own processes on a dedicated server process (stage 25b, decision E-270).
qc_add_test(test_multiplayer_bots SOURCES network/test_multiplayer_bots.cpp QcBotProcess.h LIBRARIES qc_server TIMEOUT 600)
target_compile_definitions(test_multiplayer_bots PRIVATE QTCRAFT_HEADLESS_PATH="$<TARGET_FILE:qtcraft-headless>")
add_dependencies(test_multiplayer_bots qtcraft-headless)
qc_add_test(test_creative_bots SOURCES network/test_creative_bots.cpp QcBotProcess.h LIBRARIES qc_server TIMEOUT 300)
target_compile_definitions(test_creative_bots PRIVATE QTCRAFT_HEADLESS_PATH="$<TARGET_FILE:qtcraft-headless>")
add_dependencies(test_creative_bots qtcraft-headless)
qc_add_test(test_signal_bots SOURCES network/test_signal_bots.cpp QcBotProcess.h LIBRARIES qc_server TIMEOUT 300)
target_compile_definitions(test_signal_bots PRIVATE QTCRAFT_HEADLESS_PATH="$<TARGET_FILE:qtcraft-headless>")
add_dependencies(test_signal_bots qtcraft-headless)
qc_add_test(test_machine_bots SOURCES network/test_machine_bots.cpp QcBotProcess.h LIBRARIES qc_server TIMEOUT 300)
target_compile_definitions(test_machine_bots PRIVATE QTCRAFT_HEADLESS_PATH="$<TARGET_FILE:qtcraft-headless>")
add_dependencies(test_machine_bots qtcraft-headless)

# Tests that run code on several threads or processes: server host and world generation pool,
# client session, mesh scheduler (meshing and client light on workers), texture atlas builds, TCP
# and multiplayer, headless process with its Unix signal watcher, audio output, and the game
# rendering offscreen. During a stage TSan runs only these (`scripts/verify.sh --tsan-concurrent`,
# `ctest -L concurrency`); the full verify run before the merge runs every test under TSan.
# A new test that starts threads, a server host or a process belongs in this list.
set(qc_concurrency_tests
    test_texture_atlas test_texture_pack_controller test_mesh_scheduler test_reconciliation
    test_interpolation test_client_server test_scenarios test_survival_scenario test_persistence
    test_terrain_generator test_world_backup test_world_maintenance test_audio_output
    test_tcp_transport test_network_client test_network_game test_dedicated_server test_latency
    test_creative_client test_void_client test_multiplayer_bots test_creative_bots test_signal_bots
    test_machine_bots test_headless_cli scenario_headless_walk_and_jump
    scenario_headless_unknown_command_fails scenario_headless_failure_then_quit
    app_offscreen_world_screenshot)
set_tests_properties(${qc_concurrency_tests} PROPERTIES LABELS concurrency)
