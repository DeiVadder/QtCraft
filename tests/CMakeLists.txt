# Tests are grouped by topic in subfolders (E-089); the shared fixtures stay in this folder and are
# included by their path below it ("QcServerTestFixture.h", "world/QcVillageTestUtil.h"; decision
# E-418), the code by its path below src/ through the libraries. Each
# topic folder lists its tests in tests.cmake (decision E-417), included here so that every test
# keeps this folder as its source and build directory and the properties below reach all of them.
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

include(${CMAKE_CURRENT_SOURCE_DIR}/world/tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/client/tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/content/tests.cmake)
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
    test_texture_atlas test_texture_pack_controller test_mesh_scheduler test_reconciliation
    test_interpolation test_client_server test_scenarios test_survival_scenario test_persistence
    test_terrain_generator test_village_generation test_village_plan_cache test_village_variety test_village_fields test_village_layouts test_village_swamp test_world_backup test_world_maintenance test_audio_output
    test_tcp_transport test_network_client test_network_game test_dedicated_server test_latency
    test_creative_client test_void_client test_ice_client test_multiplayer_bots test_creative_bots test_signal_bots
    test_minecart_bots test_machine_bots test_headless_cli scenario_headless_walk_and_jump
    scenario_headless_unknown_command_fails scenario_headless_failure_then_quit
    app_offscreen_world_screenshot test_chunk_store test_chunk_unloading test_chunk_tools
    test_explore_measure test_scenario_measure test_server_light)
set_tests_properties(${qc_concurrency_tests} PROPERTIES LABELS concurrency)

# Parallel runs (`ctest -j`, decision E-380): every test has its own data directory, ports are
# chosen by the system, and screenshots have names of their own. Tests that start several processes
# reserve as many job slots as they keep busy; the longest tests carry a cost so that a fresh build
# directory starts them first (later runs use the measured times in Testing/Temporary).
set_tests_properties(test_multiplayer_bots PROPERTIES PROCESSORS 4)
# Measures how punctually the server's tick timer fires; that needs a machine without other tests.
set_tests_properties(test_tick_scheduler PROPERTIES RUN_SERIAL TRUE)
# A walker, a watcher and the server in real time: next to other tests the walker's inputs cross the
# server's ticks unevenly and the walk itself stutters, which is not what it measures (E-410).
set_tests_properties(test_interpolation PROPERTIES RUN_SERIAL TRUE)
set_tests_properties(test_creative_bots test_signal_bots test_minecart_bots test_machine_bots PROPERTIES PROCESSORS 3)
set_tests_properties(test_dedicated_server test_headless_cli PROPERTIES PROCESSORS 2)
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
set_tests_properties(test_tick_control PROPERTIES COST 70)
set_tests_properties(test_scenarios test_mods test_signal_network test_server_light PROPERTIES COST 80)
set_tests_properties(test_endermen PROPERTIES COST 60)
set_tests_properties(test_village_swamp test_village_fields test_village_layouts PROPERTIES COST 40)
set_tests_properties(test_mobs test_weather_effects test_block_light_engine PROPERTIES COST 80)
set_tests_properties(test_world_details PROPERTIES COST 70)
set_tests_properties(test_minecarts test_building PROPERTIES COST 50)
set_tests_properties(test_signal_showcase test_golem_measure PROPERTIES COST 60)
set_tests_properties(test_survival_scenario test_void_client test_machine_bots test_latency
    test_creative_client test_multiplayer_bots PROPERTIES COST 30)

# Tests that mostly wait in real time (timers, links with latency, bot processes): in debug their
# CPU time is under a quarter of their wall time, and TSan slows them 1.2-3.5 x instead of 7-10 x.
# The limit check after the debug tests (scripts/verify-limits.py, decision E-412) measures them
# with a smaller factor. A new test that mostly waits belongs here.
set_property(TEST test_interpolation test_latency test_creative_client test_void_client test_ice_client
    test_survival_scenario test_multiplayer_bots test_creative_bots test_signal_bots
    test_minecart_bots test_machine_bots APPEND PROPERTY LABELS realtime)

# Inputs a test reads at run time beside its own binary: `scripts/verify.sh --changed` runs the test
# again when one of them changes (decision E-405). Tests that start qtcraft-headless name it here;
# tests that read sources compare against the folder. Runtime data (content, translations, mods,
# test data) selects every test anyway.
set_tests_properties(test_headless_cli test_dedicated_server test_multiplayer_bots test_creative_bots
    test_signal_bots test_minecart_bots test_machine_bots PROPERTIES REQUIRED_FILES "$<TARGET_FILE:qtcraft-headless>")
set_tests_properties(test_ui_palette PROPERTIES
    REQUIRED_FILES "${PROJECT_SOURCE_DIR}/src/app/qml;${PROJECT_SOURCE_DIR}/src/app/models/QcUiTheme.h")
set_tests_properties(test_server_messages PROPERTIES REQUIRED_FILES "${PROJECT_SOURCE_DIR}/src/server")
