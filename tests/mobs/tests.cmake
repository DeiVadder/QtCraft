# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Creatures: behaviour, paths, crowds, bodies, single kinds.
# Included by tests/CMakeLists.txt (decision E-417): paths are relative to tests/, and the test
# properties at the end of that file (labels, costs, required files) apply to these tests.

qc_add_test(test_mobs SOURCES mobs/test_mobs.cpp LIBRARIES qc_server TIMEOUT 600)
qc_add_test(test_mob_departures SOURCES mobs/test_mob_departures.cpp LIBRARIES qc_server)
qc_add_test(test_mob_paths SOURCES mobs/test_mob_paths.cpp fixtures/QcTestWorld.h LIBRARIES qc_server)
qc_add_test(test_mob_navigation SOURCES mobs/test_mob_navigation.cpp LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_door_steering SOURCES mobs/test_door_steering.cpp LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_mob_width SOURCES mobs/test_mob_width.cpp fixtures/QcTestWorld.h LIBRARIES qc_server)
qc_add_test(test_mob_crowds SOURCES mobs/test_mob_crowds.cpp fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_entity_push SOURCES mobs/test_entity_push.cpp fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_mob_path_measure SOURCES mobs/test_mob_path_measure.cpp LIBRARIES qc_server)
qc_add_test(test_mob_crowd_measure SOURCES mobs/test_mob_crowd_measure.cpp LIBRARIES qc_server)
qc_add_test(test_creature_parts SOURCES mobs/test_creature_parts.cpp LIBRARIES qc_server)
qc_add_test(test_body_fit SOURCES mobs/test_body_fit.cpp LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_archers SOURCES mobs/test_archers.cpp fixtures/QcServerTestFixture.h fixtures/QcTestWorld.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_spiders SOURCES mobs/test_spiders.cpp fixtures/QcServerTestFixture.h fixtures/QcTestWorld.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_witches SOURCES mobs/test_witches.cpp fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_fish SOURCES mobs/test_fish.cpp fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_silverfish SOURCES mobs/test_silverfish.cpp fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_kjut SOURCES mobs/test_kjut.cpp fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_kjut_measure SOURCES mobs/test_kjut_measure.cpp fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_shadewalkers SOURCES mobs/test_shadewalkers.cpp mobs/QcShadewalkerFixture.h fixtures/QcServerTestFixture.h fixtures/QcTestWorld.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_shadewalker_rules SOURCES mobs/test_shadewalker_rules.cpp mobs/QcShadewalkerFixture.h fixtures/QcServerTestFixture.h fixtures/QcTestWorld.h LIBRARIES qc_server TIMEOUT 600)
qc_add_test(test_creature_minds SOURCES mobs/test_creature_minds.cpp mobs/QcShadewalkerFixture.h fixtures/QcServerTestFixture.h fixtures/QcTestWorld.h LIBRARIES qc_server TIMEOUT 300)
