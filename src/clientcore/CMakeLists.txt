qt_add_library(qc_clientcore STATIC
    QcBlockRenderTable.h QcBlockRenderTable.cpp
    QcBlockVertex.h
    QcCamera.h QcCamera.cpp
    QcChunkMesh.h
    QcChunkMesher.h QcChunkMesher.cpp
    QcClientEntity.h
    QcClientSession.h QcClientSession.cpp
    QcClientWorld.h QcClientWorld.cpp
    QcEntityMesher.h QcEntityMesher.cpp
    QcGameSettings.h QcGameSettings.cpp
    QcControlInterface.h QcControlInterface.cpp
    QcMeshScheduler.h QcMeshScheduler.cpp
    QcSectionKey.h
    QcTextureAtlas.h QcTextureAtlas.cpp
    QcTextureSpec.h QcTextureSpec.cpp
    QcTextureSynthesizer.h QcTextureSynthesizer.cpp
)
qc_configure_target(qc_clientcore)
target_include_directories(qc_clientcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qc_clientcore PUBLIC qc_common Qt::Core Qt::Gui)
