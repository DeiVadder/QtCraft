# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Multiplayer: login, TCP, network games, latency and bot processes (stage 25, decisions E-251 to E-270).
# Included by tests/CMakeLists.txt (decision E-417): paths are relative to tests/, and the test
# properties at the end of that file (labels, costs, required files) apply to these tests.

# Multiplayer (stage 25, decisions E-251 to E-256).
qc_add_test(test_login_protocol SOURCES network/test_login_protocol.cpp LIBRARIES qc_network Qt::Network)
qc_add_test(test_server_login SOURCES network/test_server_login.cpp fixtures/QcServerTestFixture.h fixtures/QcRawClient.h fixtures/QcTestLogin.h LIBRARIES qc_server TIMEOUT 300)
# TLS: the server's own key and certificate (stage 48, decision E-872).
# Only with TLS: it checks the bundled OpenSSL (cmake/QcTls.cmake).
if(QC_TLS_ENABLED)
    qc_add_test(test_tls_credentials SOURCES network/test_tls_credentials.cpp LIBRARIES qc_network Qt::Network)
    target_compile_definitions(test_tls_credentials PRIVATE QC_TEST_OPENSSL_PREFIX="${QC_OPENSSL_ROOT_FOUND}")
endif()
qc_add_test(test_tcp_transport SOURCES network/test_tcp_transport.cpp fixtures/QcTestTls.h LIBRARIES qc_network Qt::Network)
# The pause of the server (decision E-877): by hand, and the keep-alives of a paused TCP link.
qc_add_test(test_server_pause SOURCES network/test_server_pause.cpp fixtures/QcServerTestFixture.h fixtures/QcRawClient.h LIBRARIES qc_server)
qc_add_test(test_pause_keepalive SOURCES network/test_pause_keepalive.cpp fixtures/QcRawClient.h LIBRARIES qc_server Qt::Network)
qc_add_test(test_network_game SOURCES network/test_network_game.cpp fixtures/QcServerTestFixture.h fixtures/QcRawClient.h fixtures/QcTestLogin.h fixtures/QcTestTls.h LIBRARIES qc_server qc_worlds TIMEOUT 180)
qc_add_test(test_dedicated_server SOURCES network/test_dedicated_server.cpp fixtures/QcRawClient.h fixtures/QcTestLogin.h LIBRARIES qc_server qc_worlds TIMEOUT 240)
target_compile_definitions(test_dedicated_server PRIVATE QTCRAFT_HEADLESS_PATH="$<TARGET_FILE:qtcraft-headless>"
                                                         QTCRAFT_MODS_SOURCE_DIR="${PROJECT_SOURCE_DIR}/mods")
add_dependencies(test_dedicated_server qtcraft-headless)
# The dedicated server's session log and diagnostics bundle (decision E-834).
qc_add_test(test_server_log SOURCES network/test_server_log.cpp fixtures/QcRawClient.h fixtures/QcTestLogin.h LIBRARIES qc_network qc_common Qt::Network TIMEOUT 120)
target_compile_definitions(test_server_log PRIVATE QTCRAFT_HEADLESS_PATH="$<TARGET_FILE:qtcraft-headless>")
add_dependencies(test_server_log qtcraft-headless)
qc_add_test(test_network_client SOURCES network/test_network_client.cpp fixtures/QcTestTls.h LIBRARIES qc_server qc_worlds qc_clientcore TIMEOUT 210)
qc_add_test(test_latency SOURCES network/test_latency.cpp fixtures/QcLatencyTransport.h LIBRARIES qc_server qc_worlds qc_clientcore TIMEOUT 270)
qc_add_test(test_adventure_client SOURCES network/test_adventure_client.cpp fixtures/QcLatencyTransport.h LIBRARIES qc_server qc_worlds qc_clientcore TIMEOUT 180)
qc_add_test(test_backpack_control SOURCES network/test_backpack_control.cpp LIBRARIES qc_server qc_worlds qc_clientcore TIMEOUT 120)
qc_add_test(test_creative_client SOURCES network/test_creative_client.cpp fixtures/QcLatencyTransport.h LIBRARIES qc_server qc_worlds qc_clientcore TIMEOUT 180)
qc_add_test(test_void_client SOURCES network/test_void_client.cpp fixtures/QcLatencyTransport.h LIBRARIES qc_server qc_worlds qc_clientcore TIMEOUT 300)
qc_add_test(test_ice_client SOURCES network/test_ice_client.cpp fixtures/QcLatencyTransport.h LIBRARIES qc_server qc_worlds qc_clientcore TIMEOUT 300)
qc_add_test(test_push_client SOURCES network/test_push_client.cpp fixtures/QcLatencyTransport.h LIBRARIES qc_server qc_worlds qc_clientcore TIMEOUT 300)
# Bot clients in their own processes on a dedicated server process (stage 25b, decision E-270).
qc_add_test(test_multiplayer_bots SOURCES network/test_multiplayer_bots.cpp fixtures/QcBotProcess.h LIBRARIES qc_server TIMEOUT 600)
target_compile_definitions(test_multiplayer_bots PRIVATE QTCRAFT_HEADLESS_PATH="$<TARGET_FILE:qtcraft-headless>")
add_dependencies(test_multiplayer_bots qtcraft-headless)
qc_add_test(test_creative_bots SOURCES network/test_creative_bots.cpp fixtures/QcBotProcess.h LIBRARIES qc_common TIMEOUT 300)
target_compile_definitions(test_creative_bots PRIVATE QTCRAFT_HEADLESS_PATH="$<TARGET_FILE:qtcraft-headless>")
add_dependencies(test_creative_bots qtcraft-headless)
qc_add_test(test_signal_bots SOURCES network/test_signal_bots.cpp fixtures/QcBotProcess.h LIBRARIES qc_common TIMEOUT 300)
target_compile_definitions(test_signal_bots PRIVATE QTCRAFT_HEADLESS_PATH="$<TARGET_FILE:qtcraft-headless>")
add_dependencies(test_signal_bots qtcraft-headless)
qc_add_test(test_minecart_bots SOURCES network/test_minecart_bots.cpp fixtures/QcBotProcess.h LIBRARIES qc_common TIMEOUT 300)
target_compile_definitions(test_minecart_bots PRIVATE QTCRAFT_HEADLESS_PATH="$<TARGET_FILE:qtcraft-headless>")
add_dependencies(test_minecart_bots qtcraft-headless)
qc_add_test(test_boat_bots SOURCES network/test_boat_bots.cpp fixtures/QcBotProcess.h LIBRARIES qc_common TIMEOUT 300)
target_compile_definitions(test_boat_bots PRIVATE QTCRAFT_HEADLESS_PATH="$<TARGET_FILE:qtcraft-headless>")
add_dependencies(test_boat_bots qtcraft-headless)
qc_add_test(test_machine_bots SOURCES network/test_machine_bots.cpp fixtures/QcBotProcess.h LIBRARIES qc_common TIMEOUT 300)
target_compile_definitions(test_machine_bots PRIVATE QTCRAFT_HEADLESS_PATH="$<TARGET_FILE:qtcraft-headless>")
add_dependencies(test_machine_bots qtcraft-headless)
# The admin channel (stage 77.1, decision E-848).
qc_add_test(test_admin_protocol SOURCES network/test_admin_protocol.cpp LIBRARIES qc_common)
qc_add_test(test_admin_channel SOURCES network/test_admin_channel.cpp network/QcAdminTestFixture.h fixtures/QcServerTestFixture.h fixtures/QcRawClient.h
            LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_admin_settings SOURCES network/test_admin_settings.cpp network/QcAdminTestFixture.h fixtures/QcServerTestFixture.h fixtures/QcRawClient.h
            LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_client_admin SOURCES network/test_client_admin.cpp LIBRARIES qc_server qc_worlds qc_clientcore TIMEOUT 180)
qc_add_test(test_advancement_client SOURCES network/test_advancement_client.cpp LIBRARIES qc_server qc_worlds qc_clientcore qc_base_content TIMEOUT 180)
