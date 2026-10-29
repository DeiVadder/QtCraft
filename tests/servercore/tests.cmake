# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Server core: commands, player management, tick, limits, chunks, input validation, backups.
# Included by tests/CMakeLists.txt (decision E-417): paths are relative to tests/, and the test
# properties at the end of that file (labels, costs, required files) apply to these tests.

qc_add_test(test_server_commands SOURCES servercore/test_server_commands.cpp fixtures/QcServerTestFixture.h fixtures/QcRawClient.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_player_management SOURCES servercore/test_player_management.cpp fixtures/QcServerTestFixture.h fixtures/QcRawClient.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_world_maintenance SOURCES servercore/test_world_maintenance.cpp fixtures/QcServerTestFixture.h fixtures/QcRawClient.h LIBRARIES qc_server TIMEOUT 240)
qc_add_test(test_tick_control SOURCES servercore/test_tick_control.cpp fixtures/QcServerTestFixture.h fixtures/QcRawClient.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_server_validation SOURCES servercore/test_server_validation.cpp fixtures/QcServerTestFixture.h fixtures/QcRawClient.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_server_limits SOURCES servercore/test_server_limits.cpp fixtures/QcServerTestFixture.h fixtures/QcRawClient.h fixtures/QcTestWorld.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_tick_scheduler SOURCES servercore/test_tick_scheduler.cpp LIBRARIES qc_server)
qc_add_test(test_tick_phases SOURCES servercore/test_tick_phases.cpp fixtures/QcServerTestFixture.h fixtures/QcRawClient.h LIBRARIES qc_server TIMEOUT 120)
qc_add_test(test_tick_systems SOURCES servercore/test_tick_systems.cpp LIBRARIES qc_server)
qc_add_test(test_block_entity_compare SOURCES servercore/test_block_entity_compare.cpp fixtures/QcDumpDiff.h fixtures/QcMachineFixture.h fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server)
target_compile_definitions(test_block_entity_compare PRIVATE QTCRAFT_COMPARE_GOLDEN="${CMAKE_CURRENT_SOURCE_DIR}/data/block-entity-compare.txt")
qc_add_test(test_entity_compare SOURCES servercore/test_entity_compare.cpp fixtures/QcMachineFixture.h fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h fixtures/QcRawClient.h LIBRARIES qc_server TIMEOUT 300)
target_compile_definitions(test_entity_compare PRIVATE QTCRAFT_COMPARE_GOLDEN="${CMAKE_CURRENT_SOURCE_DIR}/data/entity-compare-before-31-2.txt")
qc_add_test(test_chunk_tools SOURCES servercore/test_chunk_tools.cpp fixtures/QcMachineFixture.h fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 600)
qc_add_test(test_chunk_unloading SOURCES servercore/test_chunk_unloading.cpp fixtures/QcMachineFixture.h fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h fixtures/QcBlockEntityDump.h LIBRARIES qc_server TIMEOUT 600)
qc_add_test(test_entity_model SOURCES servercore/test_entity_model.cpp fixtures/QcMachineFixture.h fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_entity_measure SOURCES servercore/test_entity_measure.cpp fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_block_entities SOURCES servercore/test_block_entities.cpp fixtures/QcMachineFixture.h fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_world_open_measure SOURCES servercore/test_world_open_measure.cpp fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 600)
# Headless measurement of the server light (stage 33.1); QTCRAFT_LIGHT_MEASURE=full for the recorded sizes.
qc_add_test(test_light_measure SOURCES servercore/test_light_measure.cpp LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_block_entity_measure SOURCES servercore/test_block_entity_measure.cpp fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_simulation_limits SOURCES servercore/test_simulation_limits.cpp fixtures/QcMachineFixture.h fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server qc_network)
qc_add_test(test_world_backup SOURCES servercore/test_world_backup.cpp fixtures/QcServerTestFixture.h fixtures/QcRawClient.h fixtures/QcTestLogin.h LIBRARIES qc_server)
