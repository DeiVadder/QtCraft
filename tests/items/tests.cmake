# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Items and blocks in the game: item data, building, farming, signs, frames, maps.
# Included by tests/CMakeLists.txt (decision E-417): paths are relative to tests/, and the test
# properties at the end of that file (labels, costs, required files) apply to these tests.

qc_add_test(test_map_store SOURCES items/test_map_store.cpp LIBRARIES qc_server)
qc_add_test(test_maps SOURCES items/test_maps.cpp fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_map_frames SOURCES items/test_map_frames.cpp fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_hanging SOURCES items/test_hanging.cpp fixtures/QcMachineFixture.h fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_server_items_and_saves SOURCES items/test_server_items_and_saves.cpp fixtures/QcServerTestFixture.h fixtures/QcRawClient.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_items SOURCES items/test_items.cpp fixtures/QcTestWorld.h LIBRARIES qc_server)
qc_add_test(test_item_data SOURCES items/test_item_data.cpp LIBRARIES qc_server)
qc_add_test(test_item_data_limits SOURCES items/test_item_data_limits.cpp LIBRARIES qc_server)
qc_add_test(test_item_data_paths SOURCES items/test_item_data_paths.cpp fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server)
qc_add_test(test_farming_server SOURCES items/test_farming_server.cpp fixtures/QcServerTestFixture.h fixtures/QcRawClient.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_dirt_paths SOURCES items/test_dirt_paths.cpp fixtures/QcServerTestFixture.h fixtures/QcRawClient.h LIBRARIES qc_server)
qc_add_test(test_building SOURCES items/test_building.cpp LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_signs SOURCES items/test_signs.cpp fixtures/QcPlayerUseFixture.h fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server qc_clientcore TIMEOUT 180)
qc_add_test(test_sign_colours SOURCES items/test_sign_colours.cpp fixtures/QcPlayerUseFixture.h fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server qc_clientcore TIMEOUT 180)
