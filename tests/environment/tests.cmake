# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# World events: explosions, fire, weather, the void, slippery ground.
# Included by tests/CMakeLists.txt (decision E-417): paths are relative to tests/, and the test
# properties at the end of that file (labels, costs, required files) apply to these tests.

qc_add_test(test_slippery_ground SOURCES environment/test_slippery_ground.cpp fixtures/QcTestWorld.h LIBRARIES qc_server)
qc_add_test(test_void_zone SOURCES environment/test_void_zone.cpp fixtures/QcServerTestFixture.h fixtures/QcRawClient.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_fire SOURCES environment/test_fire.cpp fixtures/QcMachineFixture.h fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 900)
qc_add_test(test_weather SOURCES environment/test_weather.cpp fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 900)
qc_add_test(test_weather_effects SOURCES environment/test_weather_effects.cpp fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 600)
qc_add_test(test_fire_sources SOURCES environment/test_fire_sources.cpp fixtures/QcMachineFixture.h fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_explosion_limits SOURCES environment/test_explosion_limits.cpp fixtures/QcMachineFixture.h fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 600)
qc_add_test(test_explosions SOURCES environment/test_explosions.cpp fixtures/QcMachineFixture.h fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 600)
