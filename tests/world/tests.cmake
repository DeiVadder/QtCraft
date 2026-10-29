# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# World basics: coordinates, chunks and sections, physics, block light, liquids, plant growth, blocks.
# Included by tests/CMakeLists.txt (decision E-417): paths are relative to tests/, and the test
# properties at the end of that file (labels, costs, required files) apply to these tests.

qc_add_test(test_coordinates SOURCES world/test_coordinates.cpp LIBRARIES qc_common)
qc_add_test(test_chunk SOURCES world/test_chunk.cpp LIBRARIES qc_common)
qc_add_test(test_section_snapshots SOURCES world/test_section_snapshots.cpp LIBRARIES qc_common)
qc_add_test(test_section_access SOURCES world/test_section_access.cpp LIBRARIES qc_common)
qc_add_test(test_physics SOURCES world/test_physics.cpp fixtures/QcTestWorld.h LIBRARIES qc_common)
qc_add_test(test_boat_physics SOURCES world/test_boat_physics.cpp fixtures/QcTestUtil.h fixtures/QcTestWorld.h LIBRARIES qc_base_content qc_common)
qc_add_test(test_body_push SOURCES world/test_body_push.cpp fixtures/QcTestWorld.h LIBRARIES qc_base_content qc_common)
qc_add_test(test_flight SOURCES world/test_flight.cpp fixtures/QcTestWorld.h LIBRARIES qc_common)
qc_add_test(test_plant_growth SOURCES world/test_plant_growth.cpp LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_farming SOURCES world/test_farming.cpp LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_server_light SOURCES world/test_server_light.cpp LIBRARIES qc_server TIMEOUT 600)
qc_add_test(test_interaction SOURCES world/test_interaction.cpp fixtures/QcTestWorld.h LIBRARIES qc_common)
qc_add_test(test_block_light SOURCES world/test_block_light.cpp fixtures/QcTestWorld.h LIBRARIES qc_common)
qc_add_test(test_light_engine SOURCES world/test_light_engine.cpp LIBRARIES qc_common TIMEOUT 900)
qc_add_test(test_liquids SOURCES world/test_liquids.cpp fixtures/QcTestWorld.h LIBRARIES qc_server)
qc_add_test(test_new_blocks SOURCES world/test_new_blocks.cpp fixtures/QcServerTestFixture.h fixtures/QcRawClient.h fixtures/QcTestWorld.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_kitchen_blocks SOURCES world/test_kitchen_blocks.cpp fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h fixtures/QcRawClient.h LIBRARIES qc_server TIMEOUT 180)
