qt_add_library(qc_clientcore STATIC
    QcAnimator.h QcAnimator.cpp
    QcBlockRenderTable.h QcBlockRenderTable.cpp
    QcBlockVertex.h
    QcCamera.h QcCamera.cpp
    QcChunkMesh.h
    QcChunkMesher.h QcChunkMesher.cpp
    QcClientEntity.h
    QcClientSession.h QcClientSession.cpp
    QcClientWorld.h QcClientWorld.cpp
    QcEntityMesh.h
    QcEntityMesher.h QcEntityMesher.cpp
    QcEntityPose.h
    QcEntityVertex.h
    QcGameSettings.h QcGameSettings.cpp
    QcControlInterface.h QcControlInterface.cpp
    QcMeshScheduler.h QcMeshScheduler.cpp
    QcSectionKey.h
    QcTextureAtlas.h QcTextureAtlas.cpp
    QcTextureSpec.h QcTextureSpec.cpp
    QcTextureSynthesizer.h QcTextureSynthesizer.cpp
    QcViewCamera.h QcViewCamera.cpp
)
qc_configure_target(qc_clientcore)
target_include_directories(qc_clientcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qc_clientcore PUBLIC qc_common Qt::Core Qt::Gui)
