# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

qt_add_library(qc_clientcore ${QC_LIBRARY_TYPE})
# Sources per folder, sorted, each header next to its sources (decision E-417;
# scripts/check-cmake-lists.py checks the lists).
target_sources(qc_clientcore PRIVATE
    admin/QcAdminCompletion.h admin/QcAdminCompletion.cpp
    admin/QcClientAdmin.h admin/QcClientAdmin.cpp
)
target_sources(qc_clientcore PRIVATE
    camera/QcCamera.h camera/QcCamera.cpp
    camera/QcFovEffect.h camera/QcFovEffect.cpp
    camera/QcFrustum.h camera/QcFrustum.cpp
    camera/QcLabelProjector.h camera/QcLabelProjector.cpp
    camera/QcMouseCapture.h camera/QcMouseCapture.cpp
    camera/QcMouseLook.h camera/QcMouseLook.cpp
    camera/QcRenderOrigin.h camera/QcRenderOrigin.cpp
    camera/QcViewCamera.h camera/QcViewCamera.cpp
)
target_sources(qc_clientcore PRIVATE
    control/QcControlActiveCommand.h
    control/QcControlAdminCommands.h control/QcControlAdminCommands.cpp
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
    debug/QcClientHealth.h debug/QcClientHealth.cpp
    debug/QcClientLogging.h debug/QcClientLogging.cpp
    debug/QcDebugJson.h debug/QcDebugJson.cpp
    debug/QcDebugLines.h debug/QcDebugLines.cpp
    debug/QcDebugSnapshot.h debug/QcDebugSnapshot.cpp
    debug/QcDebugText.h debug/QcDebugText.cpp
    debug/QcStatsLog.h debug/QcStatsLog.cpp
)
target_sources(qc_clientcore PRIVATE
    entitymeshes/QcBoatModel.h entitymeshes/QcBoatModel.cpp
    entitymeshes/QcBoatRiders.h entitymeshes/QcBoatRiders.cpp
    entitymeshes/QcEnchantingMesher.h entitymeshes/QcEnchantingMesher.cpp
    entitymeshes/QcEntityCulling.h entitymeshes/QcEntityCulling.cpp
    entitymeshes/QcEntityMesh.h
    entitymeshes/QcEntityMesher.h entitymeshes/QcEntityMesher.cpp
    entitymeshes/QcEntityVertex.h
    entitymeshes/QcHangingMesher.h entitymeshes/QcHangingMesher.cpp
    entitymeshes/QcHeldItemMesher.h entitymeshes/QcHeldItemMesher.cpp
    entitymeshes/QcMeshRotation.h
    entitymeshes/QcProjectileMesher.h entitymeshes/QcProjectileMesher.cpp
    entitymeshes/QcSignTextCache.h entitymeshes/QcSignTextCache.cpp
    entitymeshes/QcSignTextMesher.h entitymeshes/QcSignTextMesher.cpp
    entitymeshes/QcSpriteShape.h entitymeshes/QcSpriteShape.cpp
    entitymeshes/QcSpriteShapes.h entitymeshes/QcSpriteShapes.cpp
    entitymeshes/QcTetherMesher.h entitymeshes/QcTetherMesher.cpp
    entitymeshes/QcTethers.h entitymeshes/QcTethers.cpp
)
target_sources(qc_clientcore PRIVATE
    figures/QcAnimator.h figures/QcAnimator.cpp
    figures/QcArmourLook.h figures/QcArmourLook.cpp
    figures/QcBoxLegs.h figures/QcBoxLegs.cpp
    figures/QcCapeSwing.h figures/QcCapeSwing.cpp
    figures/QcCapeTracker.h figures/QcCapeTracker.cpp
    figures/QcEntityPose.h
    figures/QcFigureLook.h figures/QcFigureLook.cpp
    figures/QcHandSwing.h figures/QcHandSwing.cpp
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
    light/QcMovingLights.h light/QcMovingLights.cpp
    light/QcSectionLight.h light/QcSectionLight.cpp
    light/QcShadowCascades.h light/QcShadowCascades.cpp
    light/QcSky.h light/QcSky.cpp
    light/QcWaterLook.h light/QcWaterLook.cpp
)
target_sources(qc_clientcore PRIVATE
    maps/QcClientMaps.h maps/QcClientMaps.cpp
    maps/QcFramedMapAtlas.h maps/QcFramedMapAtlas.cpp
    maps/QcMapColors.h maps/QcMapColors.cpp
)
target_sources(qc_clientcore PRIVATE
    materials/QcMaterialAtlas.h materials/QcMaterialAtlas.cpp
    materials/QcMaterialMips.h materials/QcMaterialMips.cpp
    materials/QcMaterialParams.h materials/QcMaterialParams.cpp
    materials/QcMaterialRules.h materials/QcMaterialRules.cpp
    materials/QcMaterialSynthesizer.h materials/QcMaterialSynthesizer.cpp
)
target_sources(qc_clientcore PRIVATE
    meshing/QcBlockRenderTable.h meshing/QcBlockRenderTable.cpp
    meshing/QcBlockVertex.h
    meshing/QcChunkMesh.h
    meshing/QcChunkMesher.h meshing/QcChunkMesher.cpp
    meshing/QcIndexPacking.h
    meshing/QcLiquidSurface.h meshing/QcLiquidSurface.cpp
    meshing/QcMeshNeighbourhood.h
    meshing/QcMeshScheduler.h meshing/QcMeshScheduler.cpp
    meshing/QcPackedMesh.h meshing/QcPackedMesh.cpp
)
target_sources(qc_clientcore PRIVATE
    network/QcKnownServers.h network/QcKnownServers.cpp
    network/QcNetworkClient.h network/QcNetworkClient.cpp
)
target_sources(qc_clientcore PRIVATE
    player/QcAdvancementTree.h player/QcAdvancementTree.cpp
    player/QcBlockForecast.h player/QcBlockForecast.cpp
    player/QcClientAdvancements.h player/QcClientAdvancements.cpp
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
    player/QcPushBodies.h player/QcPushBodies.cpp
    player/QcSprintLatch.h player/QcSprintLatch.cpp
    player/QcTidyTreeLayout.h player/QcTidyTreeLayout.cpp
)
target_sources(qc_clientcore PRIVATE
    preview/QcFramePacer.h preview/QcFramePacer.cpp
    preview/QcPreviewActivity.h preview/QcPreviewActivity.cpp
    preview/QcPreviewCamera.h preview/QcPreviewCamera.cpp
    preview/QcPreviewHook.h preview/QcPreviewHook.cpp
    preview/QcPreviewPacer.h preview/QcPreviewPacer.cpp
    preview/QcPreviewPose.h preview/QcPreviewPose.cpp
    preview/QcPreviewScene.h preview/QcPreviewScene.cpp
)
target_sources(qc_clientcore PRIVATE
    session/QcBlockMirror.h session/QcBlockMirror.cpp
    session/QcCampfireSmoke.h session/QcCampfireSmoke.cpp
    session/QcChunkExtras.h
    session/QcClientAnvil.h session/QcClientAnvil.cpp
    session/QcClientDebugSubscription.h session/QcClientDebugSubscription.cpp
    session/QcClientEnchanting.h session/QcClientEnchanting.cpp
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
    session/QcEnchantingAura.h session/QcEnchantingAura.cpp
    session/QcExplosionEffects.h session/QcExplosionEffects.cpp
    session/QcRuneScript.h session/QcRuneScript.cpp
    session/QcServerDebugMirror.h session/QcServerDebugMirror.cpp
    session/QcSnapshotBuffer.h session/QcSnapshotBuffer.cpp
    session/QcSnapshotClock.h session/QcSnapshotClock.cpp
    session/QcWorldMirror.h session/QcWorldMirror.cpp
)
target_sources(qc_clientcore PRIVATE
    settings/QcCharacterPresets.h settings/QcCharacterPresets.cpp
    settings/QcCharacterProfile.h settings/QcCharacterProfile.cpp
    settings/QcDebugOptions.h settings/QcDebugOptions.cpp
    settings/QcDisplayOptions.h settings/QcDisplayOptions.cpp
    settings/QcGamepadProfile.h settings/QcGamepadProfile.cpp
    settings/QcGamepadSettings.h settings/QcGamepadSettings.cpp
    settings/QcGameSettings.h settings/QcGameSettings.cpp
    settings/QcKeyControls.h settings/QcKeyControls.cpp
    settings/QcKeyLabels.h settings/QcKeyLabels.cpp
    settings/QcRenderOptions.h settings/QcRenderOptions.cpp
    settings/QcScreenChoice.h settings/QcScreenChoice.cpp
    settings/QcUiPalette.h settings/QcUiPalette.cpp
    settings/QcUiScale.h settings/QcUiScale.cpp
)
target_sources(qc_clientcore PRIVATE
    sound/QcAmbience.h sound/QcAmbience.cpp
    sound/QcAudioMixer.h sound/QcAudioMixer.cpp
    sound/QcLocalSounds.h sound/QcLocalSounds.cpp
    sound/QcMusicComposer.h sound/QcMusicComposer.cpp
    sound/QcPositionalVoices.h sound/QcPositionalVoices.cpp
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
    textures/QcAnimationClock.h textures/QcAnimationClock.cpp
    textures/QcAtlasLayout.h textures/QcAtlasLayout.cpp
    textures/QcGpuLimits.h textures/QcGpuLimits.cpp
    textures/QcPixelFont.h textures/QcPixelFont.cpp
    textures/QcProceduralTexture.h textures/QcProceduralTexture.cpp
    textures/QcRuneGlyphs.h textures/QcRuneGlyphs.cpp
    textures/QcTextureArmourPatterns.h textures/QcTextureArmourPatterns.cpp
    textures/QcTextureAtlas.h textures/QcTextureAtlas.cpp
    textures/QcTextureBuildingPatterns.h textures/QcTextureBuildingPatterns.cpp
    textures/QcTextureCoatPatterns.h textures/QcTextureCoatPatterns.cpp
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
    tick/QcLatencyProbe.h tick/QcLatencyProbe.cpp
    tick/QcLinkStats.h tick/QcLinkStats.cpp
    tick/QcSessionCommand.h
    tick/QcSessionCore.h tick/QcSessionCore.cpp
    tick/QcSessionEvent.h
    tick/QcSessionLink.h tick/QcSessionLink.cpp
    tick/QcSessionThread.h tick/QcSessionThread.cpp
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
# Includes name the header by its path below src/ ("common/blockrules/QcRails.h", decision E-418):
# src/ is the only include directory a library passes on.
target_include_directories(qc_clientcore PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(qc_clientcore PUBLIC qc_common qc_network qc_texture_packs Qt::Core Qt::Concurrent Qt::Gui)

# Procedural textures must be bit-identical on every architecture (decision E-300): no fused
# multiply-add contractions in the pattern code (MSVC does not contract without /fp:contract).
set_source_files_properties(
    textures/QcTextureSynthesizer.cpp textures/QcTextureCraftPatterns.cpp textures/QcTextureFarmPatterns.cpp
    textures/QcTextureRangedPatterns.cpp textures/QcTextureArmourPatterns.cpp textures/QcTextureEmblem.cpp textures/QcTextureDetail.cpp
    textures/QcProceduralTexture.cpp textures/QcAtlasLayout.cpp textures/QcAlphaCoverage.cpp
    # Block icons as well: the projection and the depth of their faces, from the render table's
    # quads (decision E-901).
    icons/QcBlockIcon.cpp meshing/QcBlockRenderTable.cpp
    # The material maps as well (decision E-908).
    materials/QcMaterialSynthesizer.cpp materials/QcMaterialMips.cpp
    PROPERTIES COMPILE_OPTIONS "${QC_NO_FP_CONTRACT}")
