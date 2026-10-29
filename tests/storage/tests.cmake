# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Saving: chunk store, world files, world library, block and entity data.
# Included by tests/CMakeLists.txt (decision E-417): paths are relative to tests/, and the test
# properties at the end of that file (labels, costs, required files) apply to these tests.

qc_add_test(test_world_storage SOURCES storage/test_world_storage.cpp LIBRARIES qc_server)
qc_add_test(test_persistence SOURCES storage/test_persistence.cpp LIBRARIES qc_server qc_worlds qc_clientcore TIMEOUT 180)
qc_add_test(test_world_library SOURCES storage/test_world_library.cpp LIBRARIES qc_worlds)
target_compile_definitions(test_world_library PRIVATE QTCRAFT_OLD_WORLD="${CMAKE_CURRENT_SOURCE_DIR}/data/world-before-31-1")
qc_add_test(test_block_entity_components SOURCES storage/test_block_entity_components.cpp fixtures/QcServerTestFixture.h fixtures/QcStoredWorld.h LIBRARIES qc_server)
qc_add_test(test_block_entity_storage SOURCES storage/test_block_entity_storage.cpp fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
target_compile_definitions(test_block_entity_storage PRIVATE QTCRAFT_OLD_WORLD="${CMAKE_CURRENT_SOURCE_DIR}/data/world-before-31-1")
qc_add_test(test_entity_storage SOURCES storage/test_entity_storage.cpp LIBRARIES qc_server qc_worlds TIMEOUT 300)
target_compile_definitions(test_entity_storage PRIVATE QTCRAFT_OLD_WORLD="${CMAKE_CURRENT_SOURCE_DIR}/data/world-before-31-2")
qc_add_test(test_entity_components SOURCES storage/test_entity_components.cpp LIBRARIES qc_server)
qc_add_test(test_chunk_store SOURCES storage/test_chunk_store.cpp LIBRARIES qc_server)
qc_add_test(test_world_details SOURCES storage/test_world_details.cpp fixtures/QcTestWorld.h LIBRARIES qc_server TIMEOUT 450)
