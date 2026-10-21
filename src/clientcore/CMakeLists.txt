qt_add_library(qc_clientcore STATIC)
# Sources per folder, sorted, each header next to its sources (decision E-417;
# scripts/check-cmake-lists.py checks the lists).
target_sources(qc_clientcore PRIVATE
    camera/QcCamera.h camera/QcCamera.cpp
    camera/QcFovEffect.h camera/QcFovEffect.cpp
    camera/QcFrustum.h camera/QcFrustum.cpp
    camera/QcLabelProjector.h camera/QcLabelProjector.cpp
    camera/QcMouseCapture.h camera/QcMouseCapture.cpp
    camera/QcMouseLook.h camera/QcMouseLook.cpp
    camera/QcViewCamera.h camera/QcViewCamera.cpp
)
target_sources(qc_clientcore PRIVATE
    control/QcControlActiveCommand.h
    control/QcControlAim.h control/QcControlAim.cpp
    control/QcControlArguments.h control/QcControlArguments.cpp
    control/QcControlCommandTable.h control/QcControlCommandTable.cpp
    control/QcControlContext.h control/QcControlContext.cpp
    control/QcControlEntityCommands.h control/QcControlEntityCommands.cpp
    control/QcControlGamepadCommands.h control/QcControlGamepadCommands.cpp
    control/QcControlInterface.h control/QcControlInterface.cpp
    control/QcControlInventoryCommands.h control/QcControlInventoryCommands.cpp
    control/QcControlMachineCommands.h control/QcControlMachineCommands.cpp
    control/QcControlMovementCommands.h control/QcControlMovementCommands.cpp
    control/QcControlReports.h control/QcControlReports.cpp
    control/QcControlSessionCommands.h control/QcControlSessionCommands.cpp
    control/QcControlTradeCommands.h control/QcControlTradeCommands.cpp
    control/QcControlWorldCommands.h control/QcControlWorldCommands.cpp
)
target_sources(qc_clientcore PRIVATE
    debug/QcClientLogging.h debug/QcClientLogging.cpp
    debug/QcDebugJson.h debug/QcDebugJson.cpp
    debug/QcDebugLines.h debug/QcDebugLines.cpp
    debug/QcDebugSnapshot.h debug/QcDebugSnapshot.cpp
    debug/QcDebugText.h debug/QcDebugText.cpp
    debug/QcStatsLog.h debug/QcStatsLog.cpp
)
target_sources(qc_clientcore PRIVATE
    icons/QcAppIcon.h icons/QcAppIcon.cpp
    icons/QcBlockIcon.h icons/QcBlockIcon.cpp
    icons/QcItemIconSet.h icons/QcItemIconSet.cpp
    icons/QcWebHandbookAssets.h icons/QcWebHandbookAssets.cpp
)
target_sources(qc_clientcore PRIVATE
    input/QcGamepadControls.h input/QcGamepadControls.cpp
    input/QcGamepadGlyphs.h input/QcGamepadGlyphs.cpp
    input/QcGamepadMapping.h input/QcGamepadMapping.cpp
    input/QcGamepadMenuInput.h input/QcGamepadMenuInput.cpp
    input/QcGamepadResponse.h input/QcGamepadResponse.cpp
    input/QcGamepadSource.h input/QcGamepadSource.cpp
    input/QcGamepadState.h input/QcGamepadState.cpp
    input/QcGamepadTypeDetection.h input/QcGamepadTypeDetection.cpp
    input/QcHotbarRadial.h input/QcHotbarRadial.cpp
    input/QcPointerRadial.h input/QcPointerRadial.cpp
    input/QcSpatialNavigation.h input/QcSpatialNavigation.cpp
    input/QcVirtualGamepadSource.h input/QcVirtualGamepadSource.cpp
)
target_sources(qc_clientcore PRIVATE
    light/QcBlockLightEngine.h light/QcBlockLightEngine.cpp
    light/QcChunkLight.h
    light/QcEntityLight.h
    light/QcLightProbe.h light/QcLightProbe.cpp
    light/QcSectionLight.h light/QcSectionLight.cpp
    light/QcSky.h light/QcSky.cpp
)
target_sources(qc_clientcore PRIVATE
    maps/QcClientMaps.h maps/QcClientMaps.cpp
    maps/QcFramedMapAtlas.h maps/QcFramedMapAtlas.cpp
    maps/QcMapColors.h maps/QcMapColors.cpp
)
target_sources(qc_clientcore PRIVATE
    meshing/QcAnimator.h meshing/QcAnimator.cpp
    meshing/QcArmourLook.h meshing/QcArmourLook.cpp
    meshing/QcBlockRenderTable.h meshing/QcBlockRenderTable.cpp
    meshing/QcBlockVertex.h
    meshing/QcCapeSwing.h meshing/QcCapeSwing.cpp
    meshing/QcCapeTracker.h meshing/QcCapeTracker.cpp
    meshing/QcChunkMesh.h
    meshing/QcChunkMesher.h meshing/QcChunkMesher.cpp
    meshing/QcEntityMesh.h
    meshing/QcEntityMesher.h meshing/QcEntityMesher.cpp
    meshing/QcEntityPose.h
    meshing/QcEntityVertex.h
    meshing/QcFigureLook.h meshing/QcFigureLook.cpp
    meshing/QcHangingMesher.h meshing/QcHangingMesher.cpp
    meshing/QcIndexPacking.h
    meshing/QcMeshScheduler.h meshing/QcMeshScheduler.cpp
    meshing/QcPackedMesh.h meshing/QcPackedMesh.cpp
    meshing/QcProjectileMesher.h meshing/QcProjectileMesher.cpp
    meshing/QcSignTextCache.h meshing/QcSignTextCache.cpp
    meshing/QcSignTextMesher.h meshing/QcSignTextMesher.cpp
)
target_sources(qc_clientcore PRIVATE
    network/QcNetworkClient.h network/QcNetworkClient.cpp
)
target_sources(qc_clientcore PRIVATE
    player/QcClientGameMode.h player/QcClientGameMode.cpp
    player/QcClientInventory.h player/QcClientInventory.cpp
    player/QcClientRiding.h player/QcClientRiding.cpp
    player/QcClientVitals.h player/QcClientVitals.cpp
    player/QcDials.h player/QcDials.cpp
    player/QcFlightLatch.h player/QcFlightLatch.cpp
    player/QcHandActions.h player/QcHandActions.cpp
    player/QcHandTimers.h
    player/QcLocalAnimation.h player/QcLocalAnimation.cpp
    player/QcPrediction.h player/QcPrediction.cpp
    player/QcSprintLatch.h player/QcSprintLatch.cpp
)
target_sources(qc_clientcore PRIVATE
    preview/QcPreviewActivity.h preview/QcPreviewActivity.cpp
    preview/QcPreviewCamera.h preview/QcPreviewCamera.cpp
    preview/QcPreviewHook.h preview/QcPreviewHook.cpp
    preview/QcPreviewPacer.h preview/QcPreviewPacer.cpp
    preview/QcPreviewPose.h preview/QcPreviewPose.cpp
    preview/QcPreviewScene.h preview/QcPreviewScene.cpp
)
target_sources(qc_clientcore PRIVATE
    session/QcCampfireSmoke.h session/QcCampfireSmoke.cpp
    session/QcChunkExtras.h
    session/QcClientDebugSubscription.h session/QcClientDebugSubscription.cpp
    session/QcClientEntities.h session/QcClientEntities.cpp
    session/QcClientEntity.h
    session/QcClientLauncher.h session/QcClientLauncher.cpp
    session/QcClientLogin.h session/QcClientLogin.cpp
    session/QcClientMachineScreen.h session/QcClientMachineScreen.cpp
    session/QcClientSession.h session/QcClientSession.cpp
    session/QcClientSignEditor.h session/QcClientSignEditor.cpp
    session/QcClientSounds.h session/QcClientSounds.cpp
    session/QcClientStonecutter.h session/QcClientStonecutter.cpp
    session/QcClientTrading.h session/QcClientTrading.cpp
    session/QcClientWorld.h session/QcClientWorld.cpp
    session/QcExplosionEffects.h session/QcExplosionEffects.cpp
    session/QcServerDebugMirror.h session/QcServerDebugMirror.cpp
    session/QcSnapshotBuffer.h session/QcSnapshotBuffer.cpp
    session/QcSnapshotClock.h session/QcSnapshotClock.cpp
    session/QcWorldMirror.h session/QcWorldMirror.cpp
)
target_sources(qc_clientcore PRIVATE
    settings/QcCharacterPresets.h settings/QcCharacterPresets.cpp
    settings/QcCharacterProfile.h settings/QcCharacterProfile.cpp
    settings/QcDebugOptions.h settings/QcDebugOptions.cpp
    settings/QcGamepadProfile.h settings/QcGamepadProfile.cpp
    settings/QcGamepadSettings.h settings/QcGamepadSettings.cpp
    settings/QcGameSettings.h settings/QcGameSettings.cpp
    settings/QcKeyControls.h settings/QcKeyControls.cpp
    settings/QcKeyLabels.h settings/QcKeyLabels.cpp
    settings/QcScreenChoice.h settings/QcScreenChoice.cpp
    settings/QcUiPalette.h settings/QcUiPalette.cpp
    settings/QcUiScale.h settings/QcUiScale.cpp
)
target_sources(qc_clientcore PRIVATE
    sound/QcAmbience.h sound/QcAmbience.cpp
    sound/QcAudioMixer.h sound/QcAudioMixer.cpp
    sound/QcLocalSounds.h sound/QcLocalSounds.cpp
    sound/QcMusicComposer.h sound/QcMusicComposer.cpp
    sound/QcSampleBuffer.h
    sound/QcSoundBank.h sound/QcSoundBank.cpp
    sound/QcSoundCue.h
    sound/QcSoundSpatial.h sound/QcSoundSpatial.cpp
    sound/QcSoundSynthesizer.h sound/QcSoundSynthesizer.cpp
)
target_sources(qc_clientcore PRIVATE
    texturepacks/QcTexturePack.h
    texturepacks/QcTexturePackLibrary.h texturepacks/QcTexturePackLibrary.cpp
    texturepacks/QcTexturePackLoader.h texturepacks/QcTexturePackLoader.cpp
    texturepacks/QcTexturePackManifest.h texturepacks/QcTexturePackManifest.cpp
    texturepacks/QcTexturePackProblem.h texturepacks/QcTexturePackProblem.cpp
    texturepacks/QcTextureSources.h texturepacks/QcTextureSources.cpp
    texturepacks/QcTextureStackBuild.h texturepacks/QcTextureStackBuild.cpp
)
target_sources(qc_clientcore PRIVATE
    textures/QcAlphaCoverage.h textures/QcAlphaCoverage.cpp
    textures/QcAtlasLayout.h textures/QcAtlasLayout.cpp
    textures/QcGpuLimits.h textures/QcGpuLimits.cpp
    textures/QcPixelFont.h textures/QcPixelFont.cpp
    textures/QcProceduralTexture.h textures/QcProceduralTexture.cpp
    textures/QcTextureArmourPatterns.h textures/QcTextureArmourPatterns.cpp
    textures/QcTextureAtlas.h textures/QcTextureAtlas.cpp
    textures/QcTextureBuildingPatterns.h textures/QcTextureBuildingPatterns.cpp
    textures/QcTextureCraftPatterns.h textures/QcTextureCraftPatterns.cpp
    textures/QcTextureDetail.h textures/QcTextureDetail.cpp
    textures/QcTextureEmblem.h textures/QcTextureEmblem.cpp
    textures/QcTextureFarmPatterns.h textures/QcTextureFarmPatterns.cpp
    textures/QcTextureKitchenPatterns.h textures/QcTextureKitchenPatterns.cpp
    textures/QcTexturePaint.h
    textures/QcTexturePaintingPatterns.h textures/QcTexturePaintingPatterns.cpp
    textures/QcTextureRangedPatterns.h textures/QcTextureRangedPatterns.cpp
    textures/QcTextureSignalPatterns.h textures/QcTextureSignalPatterns.cpp
    textures/QcTextureSpec.h textures/QcTextureSpec.cpp
    textures/QcTextureSynthesizer.h textures/QcTextureSynthesizer.cpp
)
target_sources(qc_clientcore PRIVATE
    tick/QcSessionCommand.h
    tick/QcSessionCore.h tick/QcSessionCore.cpp
    tick/QcSessionEvent.h
    tick/QcSessionLink.h tick/QcSessionLink.cpp
    tick/QcSessionTickMode.h
    tick/QcTickPacer.h tick/QcTickPacer.cpp
    tick/QcTickTiming.h tick/QcTickTiming.cpp
)
target_sources(qc_clientcore PRIVATE
    weather/QcClientWeather.h weather/QcClientWeather.cpp
    weather/QcDropPool.h weather/QcDropPool.cpp
    weather/QcPrecipitationField.h weather/QcPrecipitationField.cpp
)
qc_configure_target(qc_clientcore)
# Includes name the header by its path below src/ ("server/blocks/QcRails.h", decision E-418):
# src/ is the only include directory a library passes on.
target_include_directories(qc_clientcore PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(qc_clientcore PUBLIC qc_common qc_network qc_texture_packs Qt::Core Qt::Concurrent Qt::Gui)

# Procedural textures must be bit-identical on every architecture (decision E-300): no fused
# multiply-add contractions in the pattern code (MSVC does not contract without /fp:contract).
set_source_files_properties(
    textures/QcTextureSynthesizer.cpp textures/QcTextureCraftPatterns.cpp textures/QcTextureFarmPatterns.cpp
    textures/QcTextureRangedPatterns.cpp textures/QcTextureArmourPatterns.cpp textures/QcTextureEmblem.cpp textures/QcTextureDetail.cpp
    textures/QcProceduralTexture.cpp textures/QcAtlasLayout.cpp textures/QcAlphaCoverage.cpp
    PROPERTIES COMPILE_OPTIONS "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-ffp-contract=off>")
