qt_add_library(qc_server STATIC
    QcBaseContent.h QcBaseContent.cpp
    QcContentPackLoader.h QcContentPackLoader.cpp
    QcLocalServerHost.h QcLocalServerHost.cpp
    QcServer.h QcServer.cpp
    QcServerConfig.h
    QcServerWorld.h QcServerWorld.cpp
    QcTestTerrainGenerator.h QcTestTerrainGenerator.cpp
    QcWorldGenerator.h
    worldgen/QcNoise.h worldgen/QcNoise.cpp
    worldgen/QcTerrainGenerator.h worldgen/QcTerrainGenerator.cpp
)
qc_configure_target(qc_server)
target_include_directories(qc_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/worldgen)
target_link_libraries(qc_server PUBLIC qc_common qc_base_content Qt::Core Qt::Concurrent)
