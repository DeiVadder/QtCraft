# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Machines and containers: furnaces, hoppers, anvil, enchanting, stonecutter, backpack, crafting.
# Included by tests/CMakeLists.txt (decision E-417): paths are relative to tests/, and the test
# properties at the end of that file (labels, costs, required files) apply to these tests.

qc_add_test(test_server_crafting SOURCES machines/test_server_crafting.cpp fixtures/QcServerTestFixture.h fixtures/QcRawClient.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_quick_move SOURCES machines/test_quick_move.cpp fixtures/QcServerTestFixture.h fixtures/QcRawClient.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_backpack SOURCES machines/test_backpack.cpp fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h fixtures/QcRawClient.h LIBRARIES qc_server)
qc_add_test(test_backpack_hand SOURCES machines/test_backpack_hand.cpp fixtures/QcServerTestFixture.h fixtures/QcRawClient.h LIBRARIES qc_server)
qc_add_test(test_backpack_world SOURCES machines/test_backpack_world.cpp fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h fixtures/QcRawClient.h LIBRARIES qc_server)
qc_add_test(test_machines SOURCES machines/test_machines.cpp fixtures/QcTestWorld.h LIBRARIES qc_server)
qc_add_test(test_enchanting SOURCES machines/test_enchanting.cpp machines/QcEnchantingFixture.h fixtures/QcPlayerUseFixture.h fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server qc_clientcore TIMEOUT 120)
qc_add_test(test_enchanting_table SOURCES machines/test_enchanting_table.cpp machines/QcEnchantingFixture.h fixtures/QcPlayerUseFixture.h fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server qc_clientcore TIMEOUT 120)
# The anvil and name tags (decision E-866).
qc_add_test(test_anvil SOURCES machines/test_anvil.cpp machines/QcEnchantingFixture.h fixtures/QcPlayerUseFixture.h fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server qc_clientcore TIMEOUT 120)
qc_add_test(test_anvil_screen SOURCES machines/test_anvil_screen.cpp machines/QcEnchantingFixture.h fixtures/QcPlayerUseFixture.h fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server qc_clientcore TIMEOUT 120)
# The decoupled Signal blocks and the anvil swap (decision E-870).
qc_add_test(test_anvil_variants SOURCES machines/test_anvil_variants.cpp machines/QcEnchantingFixture.h fixtures/QcPlayerUseFixture.h fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server qc_clientcore TIMEOUT 120)
# Named chests and machines, enchanted books, death messages with the killer (decision E-873).
qc_add_test(test_named_containers SOURCES machines/test_named_containers.cpp fixtures/QcPlayerUseFixture.h fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server qc_clientcore TIMEOUT 120)
qc_add_test(test_enchanted_books SOURCES machines/test_enchanted_books.cpp machines/QcEnchantingFixture.h fixtures/QcPlayerUseFixture.h fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server qc_clientcore TIMEOUT 120)
qc_add_test(test_stonecutter SOURCES machines/test_stonecutter.cpp fixtures/QcPlayerUseFixture.h fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server qc_clientcore TIMEOUT 120)
# Long hopper lines move one item per 8 ticks and hopper, also with 2310 hoppers (decision E-344).
qc_add_test(test_hopper_lines SOURCES machines/test_hopper_lines.cpp fixtures/QcMachineFixture.h fixtures/QcCircuitFixture.h fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 300)
