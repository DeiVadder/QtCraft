qt_add_library(qc_clientcore STATIC
    QcAmbience.h QcAmbience.cpp
    QcAnimator.h QcAnimator.cpp
    QcAudioMixer.h QcAudioMixer.cpp
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
    QcLocalSounds.h QcLocalSounds.cpp
    QcMusicComposer.h QcMusicComposer.cpp
    QcControlInterface.h QcControlInterface.cpp
    QcMeshScheduler.h QcMeshScheduler.cpp
    QcSampleBuffer.h
    QcSectionKey.h
    QcSoundBank.h QcSoundBank.cpp
    QcSoundCue.h
    QcSoundSpatial.h QcSoundSpatial.cpp
    QcSoundSynthesizer.h QcSoundSynthesizer.cpp
    QcTextureAtlas.h QcTextureAtlas.cpp
    QcTextureSpec.h QcTextureSpec.cpp
    QcTextureSynthesizer.h QcTextureSynthesizer.cpp
    QcViewCamera.h QcViewCamera.cpp
)
qc_configure_target(qc_clientcore)
target_include_directories(qc_clientcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qc_clientcore PUBLIC qc_common Qt::Core Qt::Gui)
