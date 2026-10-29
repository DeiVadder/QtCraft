# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Signal dust: circuits, pistons, hoppers, readers, traps, note block.
# Included by tests/CMakeLists.txt (decision E-417): paths are relative to tests/, and the test
# properties at the end of that file (labels, costs, required files) apply to these tests.

# Signal Dust (decisions E-333 to E-341): wires, components, pistons and their quirks.
qc_add_test(test_signal_network SOURCES signal/test_signal_network.cpp fixtures/QcTestWorld.h LIBRARIES qc_server TIMEOUT 600)
qc_add_test(test_signal_circuits SOURCES signal/test_signal_circuits.cpp fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_small_signal_torch SOURCES signal/test_small_signal_torch.cpp fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 120)
# Function blocks of package 62.2a (decisions E-765 to E-769).
qc_add_test(test_signal_indicator SOURCES signal/test_signal_indicator.cpp fixtures/QcPlayerUseFixture.h fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 120)
qc_add_test(test_note_block SOURCES signal/test_note_block.cpp fixtures/QcPlayerUseFixture.h fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 120)
qc_add_test(test_target_block SOURCES signal/test_target_block.cpp fixtures/QcPlayerUseFixture.h fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 120)
# Function blocks of package 62.3 (decisions E-782 to E-785).
qc_add_test(test_tripwire SOURCES signal/test_tripwire.cpp fixtures/QcPlayerUseFixture.h fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 120)
# Headless measurement of the signal phase; QTCRAFT_SIGNAL_MEASURE=full for the recorded sizes.
qc_add_test(test_signal_measure SOURCES signal/test_signal_measure.cpp fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_signal_bodies SOURCES signal/test_signal_bodies.cpp fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_signal_pistons SOURCES signal/test_signal_pistons.cpp fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_signal_parity SOURCES signal/test_signal_parity.cpp fixtures/QcMachineFixture.h fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_signal_hoppers SOURCES signal/test_signal_hoppers.cpp fixtures/QcMachineFixture.h fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_temple_traps SOURCES signal/test_temple_traps.cpp fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_signal_rails SOURCES signal/test_signal_rails.cpp fixtures/QcRailFixture.h fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_signal_hopper_edges SOURCES signal/test_signal_hopper_edges.cpp fixtures/QcMachineFixture.h fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_signal_dispensers SOURCES signal/test_signal_dispensers.cpp fixtures/QcMachineFixture.h fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_signal_readers SOURCES signal/test_signal_readers.cpp fixtures/QcMachineFixture.h fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
