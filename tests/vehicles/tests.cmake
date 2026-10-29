# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Minecarts, rails and boats.
# Included by tests/CMakeLists.txt (decision E-417): paths are relative to tests/, and the test
# properties at the end of that file (labels, costs, required files) apply to these tests.

qc_add_test(test_boats SOURCES vehicles/test_boats.cpp vehicles/QcBoatFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_boat_riders SOURCES vehicles/test_boat_riders.cpp vehicles/QcBoatFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_boat_measure SOURCES vehicles/test_boat_measure.cpp fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 600)
qc_add_test(test_minecarts SOURCES vehicles/test_minecarts.cpp fixtures/QcRailFixture.h fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_minecart_momentum SOURCES vehicles/test_minecart_momentum.cpp fixtures/QcRailFixture.h fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_minecart_riders SOURCES vehicles/test_minecart_riders.cpp fixtures/QcRailFixture.h fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_rail_layouts SOURCES vehicles/test_rail_layouts.cpp fixtures/QcRailFixture.h fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_cart_containers SOURCES vehicles/test_cart_containers.cpp fixtures/QcRailFixture.h fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_minecart_measure SOURCES vehicles/test_minecart_measure.cpp fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
