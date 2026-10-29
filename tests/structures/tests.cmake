# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Structures of the generation: temples, dungeons, mineshafts, towers, ruins, buildings.
# Included by tests/CMakeLists.txt (decision E-417): paths are relative to tests/, and the test
# properties at the end of that file (labels, costs, required files) apply to these tests.

qc_add_test(test_dungeons SOURCES structures/test_dungeons.cpp LIBRARIES qc_worldgen qc_base_content TIMEOUT 480)
qc_add_test(test_locate_places SOURCES structures/test_locate_places.cpp LIBRARIES qc_worldgen qc_base_content TIMEOUT 600)
qc_add_test(test_mineshaft_inclines SOURCES structures/test_mineshaft_inclines.cpp LIBRARIES qc_worldgen qc_base_content TIMEOUT 750)
qc_add_test(test_mineshafts SOURCES structures/test_mineshafts.cpp LIBRARIES qc_worldgen qc_base_content TIMEOUT 660)
qc_add_test(test_coastal_towers SOURCES structures/test_coastal_towers.cpp LIBRARIES qc_worldgen qc_base_content TIMEOUT 750)
qc_add_test(test_desert_temples SOURCES structures/test_desert_temples.cpp LIBRARIES qc_worldgen qc_base_content TIMEOUT 600)
qc_add_test(test_jungle_temples SOURCES structures/test_jungle_temples.cpp LIBRARIES qc_worldgen qc_base_content TIMEOUT 600)
qc_add_test(test_temple_entrances SOURCES structures/test_temple_entrances.cpp LIBRARIES qc_worldgen qc_base_content TIMEOUT 600)
qc_add_test(test_surface_ruins SOURCES structures/test_surface_ruins.cpp LIBRARIES qc_worldgen qc_base_content TIMEOUT 600)
qc_add_test(test_structures SOURCES structures/test_structures.cpp LIBRARIES qc_base_content qc_worldgen)
qc_add_test(test_building_shapes SOURCES structures/test_building_shapes.cpp fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h fixtures/QcRawClient.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_building_variants SOURCES structures/test_building_variants.cpp fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h fixtures/QcRawClient.h LIBRARIES qc_server TIMEOUT 180)
