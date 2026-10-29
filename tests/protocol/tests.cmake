# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Protocol, codecs and transports.
# Included by tests/CMakeLists.txt (decision E-417): paths are relative to tests/, and the test
# properties at the end of that file (labels, costs, required files) apply to these tests.

qc_add_test(test_protocol SOURCES protocol/test_protocol.cpp LIBRARIES qc_common)
qc_add_test(test_chunk_protocol SOURCES protocol/test_chunk_protocol.cpp LIBRARIES qc_common)
qc_add_test(test_entity_protocol SOURCES protocol/test_entity_protocol.cpp LIBRARIES qc_common)
qc_add_test(test_game_mode_protocol SOURCES protocol/test_game_mode_protocol.cpp LIBRARIES qc_common)
# Multiplayer (stage 25, decisions E-251 to E-256).
qc_add_test(test_login_protocol SOURCES protocol/test_login_protocol.cpp LIBRARIES qc_network Qt::Network)
# TLS: the server's own key and certificate (stage 48, decision E-872).
# Only with TLS: it checks the bundled OpenSSL (cmake/QcTls.cmake).
if(QC_TLS_ENABLED)
    qc_add_test(test_tls_credentials SOURCES protocol/test_tls_credentials.cpp LIBRARIES qc_network Qt::Network)
    target_compile_definitions(test_tls_credentials PRIVATE QC_TEST_OPENSSL_PREFIX="${QC_OPENSSL_ROOT_FOUND}")
endif()
qc_add_test(test_tcp_transport SOURCES protocol/test_tcp_transport.cpp fixtures/QcTestTls.h LIBRARIES qc_network Qt::Network)
qc_add_test(test_latency SOURCES protocol/test_latency.cpp fixtures/QcLatencyTransport.h LIBRARIES qc_server qc_worlds qc_clientcore TIMEOUT 270)
# The admin channel (stage 77.1, decision E-848).
qc_add_test(test_admin_protocol SOURCES protocol/test_admin_protocol.cpp LIBRARIES qc_common)
