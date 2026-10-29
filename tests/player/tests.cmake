# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Player: game modes, health, hunger, effects, experience, death, armour, combat, projectiles, fishing, advancements.
# Included by tests/CMakeLists.txt (decision E-417): paths are relative to tests/, and the test
# properties at the end of that file (labels, costs, required files) apply to these tests.

qc_add_test(test_adventure_rules SOURCES player/test_adventure_rules.cpp LIBRARIES qc_common)
qc_add_test(test_armour_server SOURCES player/test_armour_server.cpp fixtures/QcServerTestFixture.h fixtures/QcRawClient.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_armour_stands SOURCES player/test_armour_stands.cpp fixtures/QcServerTestFixture.h fixtures/QcRawClient.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_hunger SOURCES player/test_hunger.cpp LIBRARIES qc_server)
qc_add_test(test_adventure_mode SOURCES player/test_adventure_mode.cpp fixtures/QcServerTestFixture.h fixtures/QcRawClient.h LIBRARIES qc_server qc_worlds TIMEOUT 180)
qc_add_test(test_creative_mode SOURCES player/test_creative_mode.cpp fixtures/QcServerTestFixture.h fixtures/QcRawClient.h LIBRARIES qc_server qc_worlds TIMEOUT 180)
qc_add_test(test_spectator_mode SOURCES player/test_spectator_mode.cpp fixtures/QcServerTestFixture.h fixtures/QcRawClient.h LIBRARIES qc_server qc_worlds TIMEOUT 180)
qc_add_test(test_death_messages SOURCES player/test_death_messages.cpp fixtures/QcPlayerUseFixture.h fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server qc_clientcore TIMEOUT 120)
qc_add_test(test_experience SOURCES player/test_experience.cpp fixtures/QcServerTestFixture.h fixtures/QcTestWorld.h LIBRARIES qc_server qc_clientcore TIMEOUT 300)
qc_add_test(test_ammunition SOURCES player/test_ammunition.cpp fixtures/QcServerTestFixture.h LIBRARIES qc_server qc_clientcore TIMEOUT 300)
qc_add_test(test_status_effects SOURCES player/test_status_effects.cpp fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_fishing SOURCES player/test_fishing.cpp fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_projectiles SOURCES player/test_projectiles.cpp fixtures/QcServerTestFixture.h fixtures/QcTestWorld.h LIBRARIES qc_server qc_clientcore TIMEOUT 300)
qc_add_test(test_experience_sources SOURCES player/test_experience_sources.cpp fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_projectile_measure SOURCES player/test_projectile_measure.cpp fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_invulnerability SOURCES player/test_invulnerability.cpp fixtures/QcServerTestFixture.h LIBRARIES qc_server qc_worlds TIMEOUT 120)
qc_add_test(test_advancements SOURCES player/test_advancements.cpp fixtures/QcServerTestFixture.h LIBRARIES qc_server qc_base_content TIMEOUT 300)
