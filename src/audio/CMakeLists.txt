# Audio output of the graphical client (Qt Multimedia). Mixing and synthesis live in qc_clientcore,
# which stays free of audio devices so that tests can use them anywhere.
qt_add_library(qc_audio STATIC)
# Sources sorted, each header next to its sources (decision E-417;
# scripts/check-cmake-lists.py checks the lists).
target_sources(qc_audio PRIVATE
    QcAudioEngine.h QcAudioEngine.cpp
    QcAudioOutput.h QcAudioOutput.cpp
    QcMixerDevice.h QcMixerDevice.cpp
)
qc_configure_target(qc_audio)
target_include_directories(qc_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qc_audio PUBLIC qc_clientcore Qt::Core Qt::Concurrent Qt::Multimedia)
