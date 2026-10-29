# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Spawning and taming: spawning, spawners, golems, animals, pets.
# Included by tests/CMakeLists.txt (decision E-417): paths are relative to tests/, and the test
# properties at the end of that file (labels, costs, required files) apply to these tests.

qc_add_test(test_monster_spawning SOURCES creatures/test_monster_spawning.cpp LIBRARIES qc_server TIMEOUT 600)
qc_add_test(test_witch_hut_spawns SOURCES creatures/test_witch_hut_spawns.cpp LIBRARIES qc_server TIMEOUT 600)
qc_add_test(test_monster_variants SOURCES creatures/test_monster_variants.cpp fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_biome_spawning SOURCES creatures/test_biome_spawning.cpp LIBRARIES qc_server TIMEOUT 600)
qc_add_test(test_spawn_measure SOURCES creatures/test_spawn_measure.cpp LIBRARIES qc_server TIMEOUT 240)
qc_add_test(test_creature_summons SOURCES creatures/test_creature_summons.cpp fixtures/QcServerTestFixture.h fixtures/QcRawClient.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_golem_summons SOURCES creatures/test_golem_summons.cpp villages/QcVillageFixture.h villages/QcVillageFixture.cpp LIBRARIES qc_server)
# Golems over whole days (decision E-470); QTCRAFT_GOLEM_MEASURE=<days> for the recorded run.
qc_add_test(test_golem_measure SOURCES creatures/test_golem_measure.cpp villages/QcVillageFixture.h villages/QcVillageFixture.cpp LIBRARIES qc_server TIMEOUT 510)
qc_add_test(test_chickens SOURCES creatures/test_chickens.cpp fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_spawners SOURCES creatures/test_spawners.cpp fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_sand_golems SOURCES creatures/test_sand_golems.cpp fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 600)
qc_add_test(test_cows SOURCES creatures/test_cows.cpp fixtures/QcServerTestFixture.h LIBRARIES qc_server qc_clientcore TIMEOUT 300)
qc_add_test(test_pet_rules SOURCES creatures/test_pet_rules.cpp creatures/QcPetFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_taming SOURCES creatures/test_taming.cpp creatures/QcPetFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_iron_golems SOURCES creatures/test_iron_golems.cpp fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
