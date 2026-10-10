qt_add_library(qc_clientcore STATIC
    sound/QcAmbience.h sound/QcAmbience.cpp
    meshing/QcAnimator.h meshing/QcAnimator.cpp
    textures/QcAppIcon.h textures/QcAppIcon.cpp
    sound/QcAudioMixer.h sound/QcAudioMixer.cpp
    textures/QcBlockIcon.h textures/QcBlockIcon.cpp
    light/QcBlockLightEngine.h light/QcBlockLightEngine.cpp
    meshing/QcBlockRenderTable.h meshing/QcBlockRenderTable.cpp
    meshing/QcBlockVertex.h
    camera/QcCamera.h camera/QcCamera.cpp
    settings/QcKeyControls.h settings/QcKeyControls.cpp
    settings/QcKeyLabels.h settings/QcKeyLabels.cpp
    light/QcChunkLight.h
    meshing/QcChunkMesh.h
    meshing/QcChunkMesher.h meshing/QcChunkMesher.cpp
    session/QcClientEntities.h session/QcClientEntities.cpp
    session/QcClientEntity.h
    session/QcClientSession.h session/QcClientSession.cpp
    session/QcClientWorld.h session/QcClientWorld.cpp
    meshing/QcEntityMesh.h
    meshing/QcEntityMesher.h meshing/QcEntityMesher.cpp
    meshing/QcEntityPose.h
    light/QcEntityLight.h
    light/QcSky.h light/QcSky.cpp
    meshing/QcEntityVertex.h
    camera/QcFrustum.h camera/QcFrustum.cpp
    camera/QcLabelProjector.h camera/QcLabelProjector.cpp
    settings/QcGameSettings.h settings/QcGameSettings.cpp
    meshing/QcIndexPacking.h meshing/QcIndexPacking.cpp
    sound/QcLocalSounds.h sound/QcLocalSounds.cpp
    sound/QcMusicComposer.h sound/QcMusicComposer.cpp
    control/QcControlActiveCommand.h
    control/QcControlAim.h control/QcControlAim.cpp
    control/QcControlArguments.h control/QcControlArguments.cpp
    control/QcControlCommandTable.h control/QcControlCommandTable.cpp
    control/QcControlContext.h control/QcControlContext.cpp
    control/QcControlEntityCommands.h control/QcControlEntityCommands.cpp
    control/QcControlInterface.h control/QcControlInterface.cpp
    control/QcControlInventoryCommands.h control/QcControlInventoryCommands.cpp
    control/QcControlMachineCommands.h control/QcControlMachineCommands.cpp
    control/QcControlMovementCommands.h control/QcControlMovementCommands.cpp
    control/QcControlReports.h control/QcControlReports.cpp
    control/QcControlSessionCommands.h control/QcControlSessionCommands.cpp
    control/QcControlWorldCommands.h control/QcControlWorldCommands.cpp
    meshing/QcMeshScheduler.h meshing/QcMeshScheduler.cpp
    camera/QcMouseLook.h camera/QcMouseLook.cpp
    camera/QcMouseCapture.h camera/QcMouseCapture.cpp
    sound/QcSampleBuffer.h
    settings/QcScreenChoice.h settings/QcScreenChoice.cpp
    settings/QcUiPalette.h settings/QcUiPalette.cpp
    meshing/QcSectionKey.h
    sound/QcSoundBank.h sound/QcSoundBank.cpp
    sound/QcSoundCue.h
    sound/QcSoundSpatial.h sound/QcSoundSpatial.cpp
    sound/QcSoundSynthesizer.h sound/QcSoundSynthesizer.cpp
    textures/QcTextureAtlas.h textures/QcTextureAtlas.cpp
    textures/QcTextureCraftPatterns.h textures/QcTextureCraftPatterns.cpp
    textures/QcTexturePaint.h
    textures/QcTextureSpec.h textures/QcTextureSpec.cpp
    textures/QcTextureSynthesizer.h textures/QcTextureSynthesizer.cpp
    camera/QcViewCamera.h camera/QcViewCamera.cpp
)
qc_configure_target(qc_clientcore)
# Classes live in thematic subfolders (E-089); every folder is an include directory, so includes
# stay "QcName.h" without a path.
target_include_directories(qc_clientcore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/control
    ${CMAKE_CURRENT_SOURCE_DIR}/meshing
    ${CMAKE_CURRENT_SOURCE_DIR}/light
    ${CMAKE_CURRENT_SOURCE_DIR}/textures
    ${CMAKE_CURRENT_SOURCE_DIR}/camera
    ${CMAKE_CURRENT_SOURCE_DIR}/sound
    ${CMAKE_CURRENT_SOURCE_DIR}/session
    ${CMAKE_CURRENT_SOURCE_DIR}/settings)
target_link_libraries(qc_clientcore PUBLIC qc_common Qt::Core Qt::Gui)
