# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Audio output of the graphical client (Qt Multimedia). Mixing and synthesis live in qc_clientcore,
# which stays free of audio devices so that tests can use them anywhere.
qt_add_library(qc_audio ${QC_LIBRARY_TYPE})
# Sources sorted, each header next to its sources (decision E-417;
# scripts/check-cmake-lists.py checks the lists).
target_sources(qc_audio PRIVATE
    QcAudioEngine.h QcAudioEngine.cpp
    QcAudioLogging.h QcAudioLogging.cpp
    QcAudioOutput.h QcAudioOutput.cpp
    QcAudioPlatform.h
    QcAudioRoute.h QcAudioRoute.cpp
    QcAudioSinkHost.h QcAudioSinkHost.cpp
    QcMixerDevice.h QcMixerDevice.cpp
    QcSystemAudio.h QcSystemAudio.cpp
)
qc_configure_target(qc_audio)
# Includes name the header by its path below src/ ("common/blockrules/QcRails.h", decision E-418):
# src/ is the only include directory a library passes on.
target_include_directories(qc_audio PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(qc_audio PUBLIC qc_clientcore Qt::Core Qt::Concurrent Qt::Multimedia)
