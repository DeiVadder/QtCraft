# World and mod management of a process that runs worlds (decision E-705): starting the local
# server, the worlds and the data folder on disk, mods and their comparison with a server, the rule
# values of the handbook. The layer between the simulation (qc_server, linked privately: no header
# of qc_worlds includes a server header) and the programs that run worlds (qtcraft, qtcraft-headless).
qt_add_library(qc_worlds STATIC)
# Sources sorted, each header next to its sources (decision E-417;
# scripts/check-cmake-lists.py checks the lists).
target_sources(qc_worlds PRIVATE
    QcBundledMods.h QcBundledMods.cpp
    QcContentFingerprint.h QcContentFingerprint.cpp
    QcHandbookRules.h QcHandbookRules.cpp
    QcLocalServerHost.h QcLocalServerHost.cpp
    QcModLoader.h QcModLoader.cpp
    QcModStatus.h
    QcWorldLibrary.h QcWorldLibrary.cpp
    QcWorldsLogging.h QcWorldsLogging.cpp
    QcWorldStart.h
)
qc_configure_target(qc_worlds)
# Includes name the header by its path below src/ ("server/blocks/QcRails.h", decision E-418):
# src/ is the only include directory a library passes on.
target_include_directories(qc_worlds PUBLIC ${PROJECT_SOURCE_DIR}/src)
# The headers use common and network types (QcWorldInfo, QcNetworkConfig, QcLoginSettings); the
# server is an implementation detail.
target_link_libraries(qc_worlds PUBLIC qc_common qc_network Qt::Core PRIVATE qc_server)
