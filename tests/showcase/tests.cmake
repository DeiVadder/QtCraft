# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# The showcase world.
# Included by tests/CMakeLists.txt (decision E-417): paths are relative to tests/, and the test
# properties at the end of that file (labels, costs, required files) apply to these tests.

qc_add_test(test_showcase SOURCES showcase/test_showcase.cpp LIBRARIES qc_base_content qc_worldgen TIMEOUT 300)
qc_add_test(test_showcase_stations SOURCES showcase/test_showcase_stations.cpp showcase/QcShowcaseTestUtil.h fixtures/QcRawClient.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_showcase_world SOURCES showcase/test_showcase_world.cpp showcase/QcShowcaseTestUtil.h fixtures/QcRawClient.h LIBRARIES qc_server qc_worlds TIMEOUT 300)
qc_add_test(test_signal_showcase SOURCES showcase/test_signal_showcase.cpp fixtures/QcServerTestFixture.h LIBRARIES qc_server TIMEOUT 240)
qc_add_test(test_showcase_griefing SOURCES showcase/test_showcase_griefing.cpp LIBRARIES qc_server TIMEOUT 300)
