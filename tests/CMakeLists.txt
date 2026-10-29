# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Tests are grouped by topic in subfolders (E-089); the shared fixtures stay in this folder and are
# included by their path below it ("QcServerTestFixture.h", "world/QcVillageTestUtil.h"; decision
# E-418), the code by its path below src/ through the libraries. Each
# topic folder lists its tests in tests.cmake (decision E-417), included here so that every test
# keeps this folder as its source and build directory and the properties below reach all of them.
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

include(${CMAKE_CURRENT_SOURCE_DIR}/world/tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/client/tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/content/tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/diagnostics/tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/gameplay/tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/network/tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/sound/tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/ui/tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/scenarios/tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/scripts/tests.cmake)

# Tests that run code on several threads or processes: server host and world generation pool,
# client session, mesh scheduler (meshing and client light on workers), texture atlas builds, TCP
# and multiplayer, headless process with its Unix signal watcher, audio output, and the game
# rendering offscreen. During a stage TSan runs only these (`scripts/verify.sh --tsan-concurrent`,
# `ctest -L concurrency`); the full verify run before the merge runs every test under TSan.
# A new test that starts threads, a server host or a process belongs in this list.
set(qc_concurrency_tests
    test_texture_atlas test_material_maps test_texture_pack_controller test_mesh_scheduler test_reconciliation
    test_interpolation test_client_server test_scenarios test_gamepad_scenarios test_gamepad_controls test_frame_driver test_survival_scenario test_persistence
    test_terrain_generator test_village_generation test_village_plan_cache test_village_variety test_village_fields test_village_layouts test_village_swamp test_world_backup test_world_maintenance test_audio_output
    test_tcp_transport test_network_client test_network_game test_dedicated_server test_server_log test_latency test_pause_keepalive test_world_pause test_sign_controller
    test_message_log test_crash_report
    test_creative_client test_adventure_client test_advancement_client test_backpack_control test_void_client test_ice_client test_push_client test_multiplayer_bots test_creative_bots test_signal_bots
    test_minecart_bots test_machine_bots test_boat_bots test_headless_cli scenario_headless_walk_and_jump
    scenario_headless_unknown_command_fails scenario_headless_failure_then_quit test_client_admin scenario_headless_admin_channel
    app_offscreen_world_screenshot app_offscreen_view_hidden_keeps_sections test_chunk_store test_chunk_unloading test_chunk_tools
    test_explore_measure test_scenario_measure test_server_light test_session_tick test_session_thread test_section_snapshots
    test_block_prediction test_block_prediction_measure test_session_audio test_world_map_preview test_web_handbook)
set_tests_properties(${qc_concurrency_tests} PROPERTIES LABELS concurrency)

# Parallel runs (`ctest -j`, decision E-380): every test has its own data directory, ports are
# chosen by the system, and screenshots have names of their own. Tests that start several processes
# reserve as many job slots as they keep busy; the longest tests carry a cost so that a fresh build
# directory starts them first (later runs use the measured times in Testing/Temporary).
set_tests_properties(test_multiplayer_bots PROPERTIES PROCESSORS 4)
# Tests that depend on wall time or load (test_tick_scheduler, test_interpolation and others) run in
# a phase of their own after the rest instead of RUN_SERIAL: tests/timing.cmake (E-550).
set_tests_properties(test_creative_bots test_signal_bots test_minecart_bots test_machine_bots test_boat_bots PROPERTIES PROCESSORS 3)
set_tests_properties(test_dedicated_server test_server_log test_headless_cli PROPERTIES PROCESSORS 2)
# Timeouts guard against hangs, not speed: every limit is at least twice the time of the test in a
# parallel TSan run (E-380, E-410). The slowest runs are those where ctest starts the longest tests
# together (known costs first), or where the next stage builds beside the run: test_light_engine
# 425 s, test_terrain_generator 410 s, test_weather and test_fire 400 s, test_headless_cli 390 s,
# test_server_light 295 s, test_mobs, test_weather_effects and test_scenarios 270-280 s, test_mods
# 265 s, test_signal_network 255 s, test_block_light_engine 225 s; next to other tests about half
# of that. So a machine that builds beside the run does not turn a slow run into a failure.
# QtTest's limit per test function follows the test's limit (qc_add_test).
set_tests_properties(test_terrain_generator PROPERTIES COST 190)
set_tests_properties(test_headless_cli PROPERTIES COST 160)
set_tests_properties(test_light_engine PROPERTIES COST 150)
set_tests_properties(test_weather test_fire PROPERTIES COST 140)
set_tests_properties(test_tick_control test_terrain_liquids PROPERTIES COST 70)
set_tests_properties(test_scenarios test_mods test_signal_network test_server_light PROPERTIES COST 80)
set_tests_properties(test_shadewalkers test_jungle_temples test_temple_entrances test_world_map_preview PROPERTIES COST 60)
set_tests_properties(test_shadewalker_rules PROPERTIES COST 80)
set_tests_properties(test_village_swamp test_village_fields test_village_layouts PROPERTIES COST 40)
set_tests_properties(test_world_options PROPERTIES COST 25)
set_tests_properties(test_mobs test_weather_effects test_block_light_engine test_dungeons test_mineshafts test_mineshaft_inclines test_lava_lakes PROPERTIES COST 80)
set_tests_properties(test_world_details test_spawners test_desert_temples test_showcase_stations test_showcase_world test_surface_ruins PROPERTIES COST 70)
set_tests_properties(test_coastal_towers test_monster_spawning PROPERTIES COST 90)
set_tests_properties(test_biome_spawning PROPERTIES COST 80)
set_tests_properties(test_rivers PROPERTIES COST 240)
set_tests_properties(test_river_deltas PROPERTIES COST 250)
set_tests_properties(test_climate_order PROPERTIES COST 110)
set_tests_properties(test_flower_valley PROPERTIES COST 120)
set_tests_properties(test_cold_edges test_sand_golems PROPERTIES COST 80)
set_tests_properties(test_minecarts test_building PROPERTIES COST 50)
set_tests_properties(test_signal_showcase test_golem_measure test_village_plan_hash PROPERTIES COST 60)
set_tests_properties(test_survival_scenario test_void_client test_machine_bots test_latency
    test_creative_client test_multiplayer_bots test_push_client PROPERTIES COST 30)

# Tests that mostly wait in real time (timers, links with latency, bot processes): in debug their
# CPU time is under a quarter of their wall time, and TSan slows them 1.2-3.5 x instead of 7-10 x.
# The limit check after the debug tests (scripts/verify-limits.py, decision E-412) measures them
# with a smaller factor. A new test that mostly waits belongs here.
set_property(TEST test_interpolation test_latency test_creative_client test_adventure_client test_void_client test_ice_client test_push_client
    test_survival_scenario test_gamepad_scenarios test_multiplayer_bots test_creative_bots test_signal_bots
    test_minecart_bots test_machine_bots test_boat_bots test_session_tick test_session_thread app_offscreen_pad_inventory
    app_offscreen_pad_radial app_offscreen_pad_keyboard app_offscreen_pad_menu app_offscreen_pad_playstation
    app_offscreen_pad_settings app_offscreen_pad_lazy_menu app_offscreen_advancements_return APPEND PROPERTY LABELS realtime)

# Inputs a test reads at run time beside its own binary: `scripts/verify.sh --changed` runs the test
# again when one of them changes (decision E-405). Tests that start qtcraft-headless name it here;
# tests that read sources compare against the folder. Runtime data (content, translations, mods,
# test data) selects every test anyway.
set_tests_properties(test_headless_cli test_dedicated_server test_server_log test_crash_report test_multiplayer_bots test_creative_bots
    test_signal_bots test_minecart_bots test_machine_bots test_boat_bots PROPERTIES REQUIRED_FILES "$<TARGET_FILE:qtcraft-headless>")
set_tests_properties(test_web_handbook PROPERTIES REQUIRED_FILES "$<TARGET_FILE:qtcraft-headless>;${PROJECT_SOURCE_DIR}/web/handbook/search.js")
set_tests_properties(scenario_headless_admin_channel PROPERTIES
    REQUIRED_FILES "$<TARGET_FILE:qtcraft-headless>;${CMAKE_CURRENT_SOURCE_DIR}/scenarios/admin-channel.jsonl")
set_tests_properties(test_ui_palette PROPERTIES
    REQUIRED_FILES "${PROJECT_SOURCE_DIR}/src/app/qml;${PROJECT_SOURCE_DIR}/src/app/models/QcUiTheme.h")
set_tests_properties(test_content_pack PROPERTIES REQUIRED_FILES "${PROJECT_SOURCE_DIR}/src/common/content/loader/QcContentPackLoader.cpp")
set_tests_properties(test_lazy_screen PROPERTIES REQUIRED_FILES "${PROJECT_SOURCE_DIR}/src/app/qml/frames/LazyScreen.qml")
set_tests_properties(test_app_icon PROPERTIES REQUIRED_FILES "${PROJECT_SOURCE_DIR}/cmake/windows/QtCraft.ico")
set_tests_properties(test_world_options PROPERTIES REQUIRED_FILES "${PROJECT_SOURCE_DIR}/mods/rubies/pack.json")
set_tests_properties(test_generator_versions PROPERTIES REQUIRED_FILES "${CMAKE_CURRENT_SOURCE_DIR}/data/generator-versions.json")
set_tests_properties(test_server_messages PROPERTIES REQUIRED_FILES "${PROJECT_SOURCE_DIR}/src/server")
set_tests_properties(test_english_plurals PROPERTIES REQUIRED_FILES "${PROJECT_SOURCE_DIR}/translations/qtcraft_de.ts;${PROJECT_SOURCE_DIR}/translations/qtcraft_en.ts")
set_tests_properties(test_gamepad_sdl PROPERTIES
    REQUIRED_FILES "${PROJECT_SOURCE_DIR}/scripts/build-sdl.sh;${PROJECT_SOURCE_DIR}/3rdparty/sdl3-tag.txt;${PROJECT_SOURCE_DIR}/cmake/QcGamepad.cmake")
if(NOT WIN32)
    set_tests_properties(script_verify_timing_phase PROPERTIES REQUIRED_FILES "${PROJECT_SOURCE_DIR}/scripts/quiet-lock.sh;${PROJECT_SOURCE_DIR}/scripts/verify-summary.sh")
    set_tests_properties(script_verify_full_stamps PROPERTIES REQUIRED_FILES "${PROJECT_SOURCE_DIR}/scripts/verify-summary.sh")
    if(TEST script_verify_full_stamps_dash)
        set_tests_properties(script_verify_full_stamps_dash PROPERTIES REQUIRED_FILES "${PROJECT_SOURCE_DIR}/scripts/verify-summary.sh")
    endif()
    set_tests_properties(script_move_classes PROPERTIES REQUIRED_FILES "${PROJECT_SOURCE_DIR}/scripts/fix-includes.py;${PROJECT_SOURCE_DIR}/scripts/check-cmake-lists.py")
    set_tests_properties(script_check_member_access PROPERTIES REQUIRED_FILES "${PROJECT_SOURCE_DIR}/scripts/check-function-length.py;${PROJECT_SOURCE_DIR}/scripts/qc_code_lines.py")
endif()

# Tests whose result depends on wall time or machine load: label `timing`, run by scripts/verify.sh
# in a phase of their own with few jobs on a quiet machine (decision E-550).
include(${CMAKE_CURRENT_SOURCE_DIR}/timing.cmake)
