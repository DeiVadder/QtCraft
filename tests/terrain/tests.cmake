# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Terrain: noise, climate, biomes, rivers, liquids, vegetation, ores, generator versions, world types.
# Included by tests/CMakeLists.txt (decision E-417): paths are relative to tests/, and the test
# properties at the end of that file (labels, costs, required files) apply to these tests.

qc_add_test(test_noise SOURCES terrain/test_noise.cpp LIBRARIES qc_worldgen)
qc_add_test(test_giant_trees SOURCES terrain/test_giant_trees.cpp LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_biome_distribution SOURCES terrain/test_biome_distribution.cpp LIBRARIES qc_base_content qc_worldgen TIMEOUT 300)
# The terrain tests spend most of their time in the noise, which Clang/GCC debug and sanitizer
# builds optimise (decision E-900); MSVC debug builds do not and keep the limits from before. The
# limits are twice the parallel TSan times measured before E-900 (812, 826 and 446 s): TSan was not
# measured again yet.
if(MSVC)
    set(qc_flower_valley_timeout 1680)
    set(qc_rivers_timeout 2310)
    set(qc_river_deltas_timeout 3090)
else()
    set(qc_flower_valley_timeout 900)
    set(qc_rivers_timeout 1650)
    set(qc_river_deltas_timeout 1680)
endif()
qc_add_test(test_flower_valley SOURCES terrain/test_flower_valley.cpp LIBRARIES qc_base_content qc_worldgen TIMEOUT ${qc_flower_valley_timeout})
qc_add_test(test_rivers SOURCES terrain/test_rivers.cpp terrain/QcWaterTestUtil.h LIBRARIES qc_base_content qc_worldgen TIMEOUT ${qc_rivers_timeout})
qc_add_test(test_river_deltas SOURCES terrain/test_river_deltas.cpp terrain/QcWaterTestUtil.h LIBRARIES qc_base_content qc_worldgen
    TIMEOUT ${qc_river_deltas_timeout})
qc_add_test(test_cold_edges SOURCES terrain/test_cold_edges.cpp LIBRARIES qc_base_content qc_worldgen TIMEOUT 690)
qc_add_test(test_climate_order SOURCES terrain/test_climate_order.cpp LIBRARIES qc_base_content qc_worldgen TIMEOUT 720)
qc_add_test(test_spawn_ground SOURCES terrain/test_spawn_ground.cpp fixtures/QcRawClient.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_terrain_generator SOURCES terrain/test_terrain_generator.cpp terrain/QcTerrainTestUtil.h LIBRARIES qc_server TIMEOUT 900)
qc_add_test(test_lava_lakes SOURCES terrain/test_lava_lakes.cpp LIBRARIES qc_base_content qc_worldgen TIMEOUT 600)
qc_add_test(test_terrain_liquids SOURCES terrain/test_terrain_liquids.cpp terrain/QcTerrainTestUtil.h LIBRARIES qc_server TIMEOUT 900)
# Headless measurement of terrain generation (stage 69); QTCRAFT_TERRAIN_MEASURE=1 in a Release build for the recorded times.
qc_add_test(test_terrain_measure SOURCES terrain/test_terrain_measure.cpp LIBRARIES qc_base_content qc_worldgen TIMEOUT 300)
qc_add_test(test_world_types SOURCES terrain/test_world_types.cpp fixtures/QcRawClient.h LIBRARIES qc_server qc_worlds TIMEOUT 120)
# World generation options (decision E-917); reads the example mod's sources (REQUIRED_FILES at the
# end of tests/CMakeLists.txt).
qc_add_test(test_world_options SOURCES terrain/test_world_options.cpp LIBRARIES qc_server qc_worlds TIMEOUT 480)
target_compile_definitions(test_world_options PRIVATE QTCRAFT_MODS_SOURCE_DIR="${PROJECT_SOURCE_DIR}/mods")
qc_add_test(test_generator_versions SOURCES terrain/test_generator_versions.cpp LIBRARIES qc_worldgen qc_base_content TIMEOUT 300)
target_compile_definitions(test_generator_versions PRIVATE QTCRAFT_GENERATOR_REFERENCE="${CMAKE_CURRENT_SOURCE_DIR}/data/generator-versions.json")
qc_add_test(test_generator_version_worlds SOURCES terrain/test_generator_version_worlds.cpp LIBRARIES qc_server qc_worlds)
qc_add_test(test_vegetation SOURCES terrain/test_vegetation.cpp fixtures/QcServerTestFixture.h fixtures/QcRawClient.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_ores_metals SOURCES terrain/test_ores_metals.cpp fixtures/QcServerTestFixture.h fixtures/QcRawClient.h LIBRARIES qc_server TIMEOUT 180)
