# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Sound: synthesis, mixer, world sounds and the audio output.
# Included by tests/CMakeLists.txt (decision E-417): paths are relative to tests/, and the test
# properties at the end of that file (labels, costs, required files) apply to these tests.

qc_add_test(test_sound_synthesis SOURCES sound/test_sound_synthesis.cpp LIBRARIES qc_clientcore)
qc_add_test(test_audio_mixer SOURCES sound/test_audio_mixer.cpp LIBRARIES qc_clientcore)
qc_add_test(test_sound_world SOURCES sound/test_sound_world.cpp fixtures/QcTestWorld.h LIBRARIES qc_clientcore)
qc_add_test(test_sound_events SOURCES sound/test_sound_events.cpp LIBRARIES qc_server TIMEOUT 180)
# Opens the real audio device when there is one (muted, well under a second of sound) and passes without.
qc_add_test(test_audio_output SOURCES sound/test_audio_output.cpp LIBRARIES qc_audio qc_base_content TIMEOUT 90)
qc_add_test(test_sound_placement SOURCES sound/test_sound_placement.cpp LIBRARIES qc_base_content qc_clientcore)
# Following the system's output devices, on a fake device list (no real device).
qc_add_test(test_audio_route SOURCES sound/test_audio_route.cpp LIBRARIES qc_audio)
# The audio output on a fake platform: device changes, a device that does not open, sink errors.
qc_add_test(test_audio_follow SOURCES sound/test_audio_follow.cpp LIBRARIES qc_audio)
