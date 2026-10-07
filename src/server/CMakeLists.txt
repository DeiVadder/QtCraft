qt_add_library(qc_server STATIC
    QcBaseContent.h QcBaseContent.cpp
    QcContentPackLoader.h QcContentPackLoader.cpp
    QcLocalServerHost.h QcLocalServerHost.cpp
    QcMobManager.h QcMobManager.cpp
    QcServer.h QcServer.cpp
    QcServerClient.h
    QcServerGameplay.cpp
    QcServerConfig.h
    QcServerWorld.h QcServerWorld.cpp
    QcTestTerrainGenerator.h QcTestTerrainGenerator.cpp
    QcWorldGenerator.h
    QcWorldLibrary.h QcWorldLibrary.cpp
    QcWorldStorage.h QcWorldStorage.cpp
    worldgen/QcNoise.h worldgen/QcNoise.cpp
    worldgen/QcTerrainGenerator.h worldgen/QcTerrainGenerator.cpp
)
qc_configure_target(qc_server)
# Identical terrain on every compiler and CPU: no fused multiply-add contraction in noise maths.
if(MSVC)
    set_source_files_properties(worldgen/QcNoise.cpp worldgen/QcTerrainGenerator.cpp PROPERTIES COMPILE_OPTIONS /fp:precise)
else()
    set_source_files_properties(worldgen/QcNoise.cpp worldgen/QcTerrainGenerator.cpp PROPERTIES COMPILE_OPTIONS -ffp-contract=off)
endif()
target_include_directories(qc_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/worldgen)
target_link_libraries(qc_server PUBLIC qc_common qc_base_content Qt::Core Qt::Concurrent)
