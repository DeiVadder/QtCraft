# Multiplayer: login, TCP, network games, latency and bot processes (stage 25, decisions E-251 to E-270).
# Included by tests/CMakeLists.txt (decision E-417): paths are relative to tests/, and the test
# properties at the end of that file (labels, costs, required files) apply to these tests.

# Multiplayer (stage 25, decisions E-251 to E-256).
qc_add_test(test_login_protocol SOURCES network/test_login_protocol.cpp LIBRARIES qc_network Qt::Network)
qc_add_test(test_server_login SOURCES network/test_server_login.cpp QcServerTestFixture.h QcRawClient.h QcTestLogin.h LIBRARIES qc_server TIMEOUT 300)
qc_add_test(test_tcp_transport SOURCES network/test_tcp_transport.cpp LIBRARIES qc_network Qt::Network)
qc_add_test(test_network_game SOURCES network/test_network_game.cpp QcServerTestFixture.h QcRawClient.h QcTestLogin.h LIBRARIES qc_server TIMEOUT 180)
qc_add_test(test_dedicated_server SOURCES network/test_dedicated_server.cpp QcRawClient.h QcTestLogin.h LIBRARIES qc_server TIMEOUT 240)
target_compile_definitions(test_dedicated_server PRIVATE QTCRAFT_HEADLESS_PATH="$<TARGET_FILE:qtcraft-headless>"
                                                         QTCRAFT_MODS_SOURCE_DIR="${PROJECT_SOURCE_DIR}/mods")
add_dependencies(test_dedicated_server qtcraft-headless)
qc_add_test(test_network_client SOURCES network/test_network_client.cpp LIBRARIES qc_server qc_clientcore)
qc_add_test(test_latency SOURCES network/test_latency.cpp QcLatencyTransport.h LIBRARIES qc_server qc_clientcore TIMEOUT 240)
qc_add_test(test_creative_client SOURCES network/test_creative_client.cpp QcLatencyTransport.h LIBRARIES qc_server qc_clientcore TIMEOUT 180)
qc_add_test(test_void_client SOURCES network/test_void_client.cpp QcLatencyTransport.h LIBRARIES qc_server qc_clientcore TIMEOUT 300)
# Bot clients in their own processes on a dedicated server process (stage 25b, decision E-270).
qc_add_test(test_multiplayer_bots SOURCES network/test_multiplayer_bots.cpp QcBotProcess.h LIBRARIES qc_server TIMEOUT 600)
target_compile_definitions(test_multiplayer_bots PRIVATE QTCRAFT_HEADLESS_PATH="$<TARGET_FILE:qtcraft-headless>")
add_dependencies(test_multiplayer_bots qtcraft-headless)
qc_add_test(test_creative_bots SOURCES network/test_creative_bots.cpp QcBotProcess.h LIBRARIES qc_server TIMEOUT 300)
target_compile_definitions(test_creative_bots PRIVATE QTCRAFT_HEADLESS_PATH="$<TARGET_FILE:qtcraft-headless>")
add_dependencies(test_creative_bots qtcraft-headless)
qc_add_test(test_signal_bots SOURCES network/test_signal_bots.cpp QcBotProcess.h LIBRARIES qc_server TIMEOUT 300)
target_compile_definitions(test_signal_bots PRIVATE QTCRAFT_HEADLESS_PATH="$<TARGET_FILE:qtcraft-headless>")
add_dependencies(test_signal_bots qtcraft-headless)
qc_add_test(test_minecart_bots SOURCES network/test_minecart_bots.cpp QcBotProcess.h LIBRARIES qc_server TIMEOUT 300)
target_compile_definitions(test_minecart_bots PRIVATE QTCRAFT_HEADLESS_PATH="$<TARGET_FILE:qtcraft-headless>")
add_dependencies(test_minecart_bots qtcraft-headless)
qc_add_test(test_machine_bots SOURCES network/test_machine_bots.cpp QcBotProcess.h LIBRARIES qc_server TIMEOUT 300)
target_compile_definitions(test_machine_bots PRIVATE QTCRAFT_HEADLESS_PATH="$<TARGET_FILE:qtcraft-headless>")
add_dependencies(test_machine_bots qtcraft-headless)
