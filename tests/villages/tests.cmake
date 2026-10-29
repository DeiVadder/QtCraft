# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Villages: generation (qc_worldgen) and village life, trading (qc_server).
# Included by tests/CMakeLists.txt (decision E-417): paths are relative to tests/, and the test
# properties at the end of that file (labels, costs, required files) apply to these tests.

qc_add_test(test_villager_measure SOURCES villages/test_villager_measure.cpp fixtures/QcServerTestFixture.h fixtures/QcRawClient.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_villages SOURCES villages/test_villages.cpp fixtures/QcServerTestFixture.h fixtures/QcRawClient.h LIBRARIES qc_server TIMEOUT 150)
qc_add_test(test_villagers SOURCES villages/test_villagers.cpp fixtures/QcServerTestFixture.h fixtures/QcRawClient.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_trading SOURCES villages/test_trading.cpp fixtures/QcServerTestFixture.h fixtures/QcRawClient.h LIBRARIES qc_server TIMEOUT 150)
qc_add_test(test_trade_offers SOURCES villages/test_trade_offers.cpp LIBRARIES qc_server qc_clientcore)
qc_add_test(test_settlement_storage SOURCES villages/test_settlement_storage.cpp fixtures/QcServerTestFixture.h fixtures/QcRawClient.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_village_growth SOURCES villages/test_village_growth.cpp villages/QcVillageFixture.h villages/QcVillageFixture.cpp LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_village_life SOURCES villages/test_village_life.cpp villages/QcVillageFixture.h villages/QcVillageFixture.cpp LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_village_species SOURCES villages/test_village_species.cpp villages/QcVillageFixture.h villages/QcVillageFixture.cpp LIBRARIES qc_server)
qc_add_test(test_villager_walks SOURCES villages/test_villager_walks.cpp villages/QcVillageFixture.h villages/QcVillageFixture.cpp LIBRARIES qc_server TIMEOUT 300)
# Fixture villages living through whole days on a server (decision E-424); QTCRAFT_VILLAGE_SOAK_DAYS=3 for the soak.
qc_add_test(test_village_soak SOURCES villages/test_village_soak.cpp villages/QcVillageFixture.h villages/QcVillageFixture.cpp fixtures/QcServerTestFixture.h fixtures/QcRawClient.h LIBRARIES qc_server TIMEOUT 300)
# Headless measurement at the villagers' limits (decision E-424); QTCRAFT_VILLAGE_LIMITS_MEASURE=1 for the recorded run.
qc_add_test(test_village_limits_measure SOURCES villages/test_village_limits_measure.cpp villages/QcVillageFixture.h villages/QcVillageFixture.cpp fixtures/QcServerTestFixture.h fixtures/QcRawClient.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_village_generation SOURCES villages/test_village_generation.cpp villages/QcVillageTestUtil.h LIBRARIES qc_base_content qc_worldgen)
qc_add_test(test_village_plan_cache SOURCES villages/test_village_plan_cache.cpp villages/QcVillageTestUtil.h LIBRARIES qc_server)
qc_add_test(test_village_variety SOURCES villages/test_village_variety.cpp villages/QcVillageTestUtil.h LIBRARIES qc_base_content qc_worldgen TIMEOUT 300)
qc_add_test(test_village_fields SOURCES villages/test_village_fields.cpp villages/QcVillageTestUtil.h LIBRARIES qc_base_content qc_worldgen TIMEOUT 300)
qc_add_test(test_village_layouts SOURCES villages/test_village_layouts.cpp villages/QcVillageTestUtil.h LIBRARIES qc_base_content qc_worldgen TIMEOUT 300)
qc_add_test(test_village_bounds SOURCES villages/test_village_bounds.cpp villages/QcVillageTestUtil.h LIBRARIES qc_base_content qc_worldgen TIMEOUT 300)
qc_add_test(test_village_plan_hash SOURCES villages/test_village_plan_hash.cpp LIBRARIES qc_base_content qc_worldgen TIMEOUT 300)
qc_add_test(test_village_swamp SOURCES villages/test_village_swamp.cpp villages/QcVillageTestUtil.h LIBRARIES qc_server TIMEOUT 360)
# Headless measurement of village generation (stage 37.2); QTCRAFT_VILLAGE_MEASURE=1 for the recorded sizes.
qc_add_test(test_village_measure SOURCES villages/test_village_measure.cpp LIBRARIES qc_base_content qc_worldgen TIMEOUT 300)
