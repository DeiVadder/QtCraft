# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Client session: tick, prediction, reconciliation, interpolation, light, weather, maps, the session's sound.
# Included by tests/CMakeLists.txt (decision E-417): paths are relative to tests/, and the test
# properties at the end of that file (labels, costs, required files) apply to these tests.

# Maps on the client (decisions E-801, E-802): the cache, the colours from the atlas, the image.
qc_add_test(test_client_maps SOURCES session/test_client_maps.cpp LIBRARIES qc_clientcore)
# The threads between Kjuts and their children (decision E-839).
qc_add_test(test_kjut_threads SOURCES session/test_kjut_threads.cpp LIBRARIES qc_base_content qc_clientcore)
qc_add_test(test_weather_client SOURCES session/test_weather_client.cpp fixtures/QcTestWorld.h LIBRARIES qc_clientcore)
# The prediction of the local player on its own (decision E-530): reconciliation, pending inputs, riding.
# The client session's tick thread (decision E-560): pacer and core on a fake clock, long frames of
# the session's thread (real time), order of commands, ending sessions, the fallback without a thread.
qc_add_test(test_session_tick SOURCES session/test_session_tick.cpp fixtures/QcRecordingTransport.h LIBRARIES qc_server qc_worlds qc_clientcore)
# The session on its tick thread against a server: wish to fly after landing, server stop and kick,
# death and respawn, riding, the same walk as with the timer in the session's thread.
qc_add_test(test_session_thread SOURCES session/test_session_thread.cpp fixtures/QcRecordingTransport.h fixtures/QcTestTls.h LIBRARIES qc_server qc_worlds qc_clientcore)
qc_add_test(test_prediction SOURCES session/test_prediction.cpp fixtures/QcTestWorld.h LIBRARIES qc_clientcore)
qc_add_test(test_push_bodies SOURCES session/test_push_bodies.cpp LIBRARIES qc_base_content qc_clientcore)
qc_add_test(test_debug_report SOURCES session/test_debug_report.cpp fixtures/QcTestWorld.h LIBRARIES qc_clientcore)
qc_add_test(test_reconciliation SOURCES session/test_reconciliation.cpp fixtures/QcTestWorld.h LIBRARIES qc_clientcore)
qc_add_test(test_interpolation SOURCES session/test_interpolation.cpp fixtures/QcLatencyTransport.h LIBRARIES qc_server qc_worlds qc_clientcore)
qc_add_test(test_block_mirror SOURCES session/test_block_mirror.cpp LIBRARIES qc_base_content qc_clientcore)
qc_add_test(test_block_prediction SOURCES session/test_block_prediction.cpp fixtures/QcPredictionFixture.h fixtures/QcSessionMeshes.h
    LIBRARIES qc_server qc_worlds qc_clientcore TIMEOUT 150)
qc_add_test(test_block_prediction_measure SOURCES session/test_block_prediction_measure.cpp fixtures/QcPredictionFixture.h fixtures/QcSessionMeshes.h
    LIBRARIES qc_server qc_worlds qc_clientcore)
qc_add_test(test_moving_lights SOURCES session/test_moving_lights.cpp LIBRARIES qc_base_content qc_clientcore)
qc_add_test(test_block_light_engine SOURCES session/test_block_light_engine.cpp LIBRARIES qc_clientcore TIMEOUT 500)
# The session's sounds reach the audio engine each tick (QcSessionAudio, part of QcSessionWiring).
qc_add_test(test_session_audio SOURCES session/test_session_audio.cpp LIBRARIES qc_server qc_app_core qc_worlds)
# Field of view while sprinting and the sprint latch (decision E-280).
qc_add_test(test_sprint_feel SOURCES session/test_sprint_feel.cpp LIBRARIES qc_clientcore)
qc_add_test(test_world_pause SOURCES session/test_world_pause.cpp fixtures/QcServerTestFixture.h fixtures/QcRawClient.h LIBRARIES qc_app_core qc_worlds qc_server TIMEOUT 150)
