qt_add_library(qc_clientcore STATIC
    QcAmbience.h QcAmbience.cpp
    QcAnimator.h QcAnimator.cpp
    QcAppIcon.h QcAppIcon.cpp
    QcAudioMixer.h QcAudioMixer.cpp
    QcBlockIcon.h QcBlockIcon.cpp
    QcBlockLightEngine.h QcBlockLightEngine.cpp
    QcBlockRenderTable.h QcBlockRenderTable.cpp
    QcBlockVertex.h
    QcCamera.h QcCamera.cpp
    QcKeyControls.h QcKeyControls.cpp
    QcKeyLabels.h QcKeyLabels.cpp
    QcChunkLight.h
    QcChunkMesh.h
    QcChunkMesher.h QcChunkMesher.cpp
    QcClientEntities.h QcClientEntities.cpp
    QcClientEntity.h
    QcClientSession.h QcClientSession.cpp
    QcClientWorld.h QcClientWorld.cpp
    QcEntityMesh.h
    QcEntityMesher.h QcEntityMesher.cpp
    QcEntityPose.h
    QcEntityLight.h
    QcEntityVertex.h
    QcFrustum.h QcFrustum.cpp
    QcGameSettings.h QcGameSettings.cpp
    QcIndexPacking.h QcIndexPacking.cpp
    QcLocalSounds.h QcLocalSounds.cpp
    QcMusicComposer.h QcMusicComposer.cpp
    QcControlActiveCommand.h
    QcControlAim.h QcControlAim.cpp
    QcControlArguments.h QcControlArguments.cpp
    QcControlCommandTable.h QcControlCommandTable.cpp
    QcControlContext.h QcControlContext.cpp
    QcControlEntityCommands.h QcControlEntityCommands.cpp
    QcControlInterface.h QcControlInterface.cpp
    QcControlInventoryCommands.h QcControlInventoryCommands.cpp
    QcControlMachineCommands.h QcControlMachineCommands.cpp
    QcControlMovementCommands.h QcControlMovementCommands.cpp
    QcControlReports.h QcControlReports.cpp
    QcControlSessionCommands.h QcControlSessionCommands.cpp
    QcControlWorldCommands.h QcControlWorldCommands.cpp
    QcMeshScheduler.h QcMeshScheduler.cpp
    QcMouseLook.h QcMouseLook.cpp
    QcSampleBuffer.h
    QcScreenChoice.h QcScreenChoice.cpp
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
