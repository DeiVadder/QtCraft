# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

qt_add_library(qc_common ${QC_LIBRARY_TYPE})
# Sources per folder, sorted, each header next to its sources (decision E-417;
# scripts/check-cmake-lists.py checks the lists).
target_sources(qc_common PRIVATE
    auth/QcLoginCrypto.h auth/QcLoginCrypto.cpp
    auth/QcOwnerOnlyAccess.h auth/QcOwnerOnlyAccess.cpp
    auth/QcPlayerKey.h auth/QcPlayerKey.cpp
    auth/QcProfileValidation.h auth/QcProfileValidation.cpp
    auth/QcSecretFile.h auth/QcSecretFile.cpp
)
target_sources(qc_common PRIVATE
    blockrules/QcBlockRules.h blockrules/QcBlockRules.cpp
    blockrules/QcRails.h blockrules/QcRails.cpp
    blockrules/QcSlabRules.h blockrules/QcSlabRules.cpp
    blockrules/QcStairShapes.h blockrules/QcStairShapes.cpp
)
target_sources(qc_common PRIVATE
    content/blockbehaviour/QcBlastDefinition.h
    content/blockbehaviour/QcBlastParser.h content/blockbehaviour/QcBlastParser.cpp
    content/blockbehaviour/QcFireDefinition.h
    content/blockbehaviour/QcHangingDefinition.h
    content/blockbehaviour/QcHangingParser.h content/blockbehaviour/QcHangingParser.cpp
    content/blockbehaviour/QcLiquidDefinition.h
    content/blockbehaviour/QcLiquidParser.h content/blockbehaviour/QcLiquidParser.cpp
    content/blockbehaviour/QcLiquidReaction.h
    content/blockbehaviour/QcRailParser.h content/blockbehaviour/QcRailParser.cpp
    content/blockbehaviour/QcSignalDefinition.h
    content/blockbehaviour/QcSignalLinks.h content/blockbehaviour/QcSignalLinks.cpp
    content/blockbehaviour/QcSignalParser.h content/blockbehaviour/QcSignalParser.cpp
    content/blockbehaviour/QcSpawnerContent.h content/blockbehaviour/QcSpawnerContent.cpp
    content/blockbehaviour/QcSpawnerDefinition.h
)
target_sources(qc_common PRIVATE
    content/blocks/QcBlockDefinition.h
    content/blocks/QcBlockParser.h content/blocks/QcBlockParser.cpp
    content/blocks/QcBlockProperty.h
    content/blocks/QcBlockSerializer.h content/blocks/QcBlockSerializer.cpp
    content/blocks/QcBlockState.h
    content/blocks/QcBlockStateTable.h content/blocks/QcBlockStateTable.cpp
    content/blocks/QcBlockTextureParser.h content/blocks/QcBlockTextureParser.cpp
    content/blocks/QcMaterialDefinition.h
    content/blocks/QcMaterialParser.h content/blocks/QcMaterialParser.cpp
    content/blocks/QcPartFamily.h
    content/blocks/QcPartGenerator.h content/blocks/QcPartGenerator.cpp
    content/blocks/QcShapeCondition.h
    content/blocks/QcShapeDefinition.h
    content/blocks/QcShapeParser.h content/blocks/QcShapeParser.cpp
    content/blocks/QcShapePart.h
    content/blocks/QcTextureAnimation.h content/blocks/QcTextureAnimation.cpp
    content/blocks/QcTextureDefinition.h
)
target_sources(qc_common PRIVATE
    content/items/QcAnvilSwap.h
    content/items/QcAnvilSwaps.h content/items/QcAnvilSwaps.cpp
    content/items/QcArmourDefinition.h
    content/items/QcArmourParser.h content/items/QcArmourParser.cpp
    content/items/QcArmourSerializer.h content/items/QcArmourSerializer.cpp
    content/items/QcEffectDefinition.h
    content/items/QcEffectParser.h content/items/QcEffectParser.cpp
    content/items/QcEnchantmentDefinition.h
    content/items/QcEnchantmentParser.h content/items/QcEnchantmentParser.cpp
    content/items/QcEnchantmentSerializer.h content/items/QcEnchantmentSerializer.cpp
    content/items/QcExperienceDrop.h
    content/items/QcHitEffect.h
    content/items/QcHoldDisplay.h content/items/QcHoldDisplay.cpp
    content/items/QcHoldParser.h content/items/QcHoldParser.cpp
    content/items/QcHoldSerializer.h content/items/QcHoldSerializer.cpp
    content/items/QcItemDefinition.h content/items/QcItemDefinition.cpp
    content/items/QcItemParser.h content/items/QcItemParser.cpp
    content/items/QcItemSerializer.h content/items/QcItemSerializer.cpp
    content/items/QcProjectileDefinition.h
    content/items/QcProjectileParser.h content/items/QcProjectileParser.cpp
)
target_sources(qc_common PRIVATE
    content/loader/QcContentPackLoader.h content/loader/QcContentPackLoader.cpp
)
target_sources(qc_common PRIVATE
    content/mobs/QcAppearance.h
    content/mobs/QcAppearanceDefinition.h
    content/mobs/QcMobDefinition.h
    content/mobs/QcMobParser.h content/mobs/QcMobParser.cpp
    content/mobs/QcMobReader.h content/mobs/QcMobReader.cpp content/mobs/QcMobReaderTraits.cpp
    content/mobs/QcMobSerializer.h content/mobs/QcMobSerializer.cpp
    content/mobs/QcMobTraits.h
    content/mobs/QcMobTraitsContent.h content/mobs/QcMobTraitsContent.cpp
    content/mobs/QcModelDefinition.h
    content/mobs/QcSpawnWeights.h content/mobs/QcSpawnWeights.cpp
)
target_sources(qc_common PRIVATE
    content/recipes/QcCookingDefinitions.h
    content/recipes/QcCookingParser.h content/recipes/QcCookingParser.cpp
    content/recipes/QcFarmingDefinitions.h
    content/recipes/QcFarmingParser.h content/recipes/QcFarmingParser.cpp
    content/recipes/QcLootTableDefinition.h
    content/recipes/QcLootTableParser.h content/recipes/QcLootTableParser.cpp
    content/recipes/QcLootTables.h content/recipes/QcLootTables.cpp
    content/recipes/QcMachineDefinition.h
    content/recipes/QcRecipeDefinition.h content/recipes/QcRecipeDefinition.cpp
    content/recipes/QcRecipeParser.h content/recipes/QcRecipeParser.cpp
    content/recipes/QcSmeltingRecipe.h
    content/recipes/QcStonecutting.h content/recipes/QcStonecutting.cpp
)
target_sources(qc_common PRIVATE
    content/registry/QcAdvancementCatalog.h content/registry/QcAdvancementCatalog.cpp
    content/registry/QcAdvancementDefinition.h
    content/registry/QcAdvancementParser.h content/registry/QcAdvancementParser.cpp
    content/registry/QcAdvancementSerializer.h content/registry/QcAdvancementSerializer.cpp
    content/registry/QcAssetCatalog.h
    content/registry/QcCommandMacroDefinition.h
    content/registry/QcCommandMacroParser.h content/registry/QcCommandMacroParser.cpp
    content/registry/QcContentFields.h content/registry/QcContentFields.cpp
    content/registry/QcContentNames.h content/registry/QcContentNames.cpp
    content/registry/QcContentParser.h content/registry/QcContentParser.cpp
    content/registry/QcContentRegistry.h content/registry/QcContentRegistry.cpp
    content/registry/QcContentSerializer.h content/registry/QcContentSerializer.cpp
    content/registry/QcJsonFieldReader.h content/registry/QcJsonFieldReader.cpp
    content/registry/QcNamedList.h
    content/registry/QcPackReference.h
    content/registry/QcSoundDefinition.h content/registry/QcSoundDefinition.cpp
    content/registry/QcSoundKeys.h content/registry/QcSoundKeys.cpp
    content/registry/QcSoundParser.h content/registry/QcSoundParser.cpp
    content/registry/QcTranslations.h content/registry/QcTranslations.cpp
)
target_sources(qc_common PRIVATE
    content/villages/QcTradeDefinitions.h
    content/villages/QcTradeParser.h content/villages/QcTradeParser.cpp
    content/villages/QcVillageCatalog.h content/villages/QcVillageCatalog.cpp
    content/villages/QcVillageContent.h content/villages/QcVillageContent.cpp
    content/villages/QcVillageDefinitions.h
    content/villages/QcVillageLayoutParser.h content/villages/QcVillageLayoutParser.cpp
    content/villages/QcVillageParser.h content/villages/QcVillageParser.cpp
    content/villages/QcVillagerDefinitions.h
    content/villages/QcVillagerOutfits.h content/villages/QcVillagerOutfits.cpp
    content/villages/QcVillagerParser.h content/villages/QcVillagerParser.cpp
)
target_sources(qc_common PRIVATE
    content/worldgen/QcCoastalTowerDefinition.h
    content/worldgen/QcCoastalTowerParser.h content/worldgen/QcCoastalTowerParser.cpp
    content/worldgen/QcDungeonDefinition.h
    content/worldgen/QcMineshaftDefinition.h
    content/worldgen/QcShowcaseDefinition.h
    content/worldgen/QcShowcaseParser.h content/worldgen/QcShowcaseParser.cpp
    content/worldgen/QcStructureDefinition.h
    content/worldgen/QcStructureParser.h content/worldgen/QcStructureParser.cpp
    content/worldgen/QcStructureSiteParser.h content/worldgen/QcStructureSiteParser.cpp
    content/worldgen/QcStructureVariationParser.h content/worldgen/QcStructureVariationParser.cpp
    content/worldgen/QcVegetationDefinition.h
    content/worldgen/QcVegetationParser.h content/worldgen/QcVegetationParser.cpp
    content/worldgen/QcWeatherDefinition.h
    content/worldgen/QcWeatherParser.h content/worldgen/QcWeatherParser.cpp
    content/worldgen/QcWorldgenCatalog.h
    content/worldgen/QcWorldgenDefinitions.h
    content/worldgen/QcWorldgenParser.h content/worldgen/QcWorldgenParser.cpp
    content/worldgen/QcWorldOptionDefinitions.h
    content/worldgen/QcWorldOptionParser.h content/worldgen/QcWorldOptionParser.cpp
    content/worldgen/QcWorldTypeDefinitions.h
    content/worldgen/QcWorldTypeParser.h content/worldgen/QcWorldTypeParser.cpp
)
target_sources(qc_common PRIVATE
    core/QcAabb.h
    core/QcBlockFace.h
    core/QcBlockPos.h core/QcBlockPos.cpp
    core/QcChunkPos.h core/QcChunkPos.cpp
    core/QcCommandCategory.h core/QcCommandCategory.cpp
    core/QcCommandSyntax.h core/QcCommandSyntax.cpp
    core/QcCrc32.h core/QcCrc32.cpp
    core/QcDataRoot.h core/QcDataRoot.cpp
    core/QcDowncast.h
    core/QcIntMath.h
    core/QcKeptComponent.h
    core/QcLogging.h core/QcLogging.cpp
    core/QcMailbox.h
    core/QcNameText.h core/QcNameText.cpp
    core/QcPhaseWindow.h core/QcPhaseWindow.cpp
    core/QcProcessMemory.h core/QcProcessMemory.cpp
    core/QcResult.h
    core/QcSectionKey.h
    core/QcTickPhase.h
    core/QcTickSummary.h
    core/QcVec3.h
    core/QcWorkerPool.h core/QcWorkerPool.cpp
    core/QcWorldConstants.h
)
target_sources(qc_common PRIVATE
    diagnostics/QcBuildInfo.h diagnostics/QcBuildInfo.cpp
    diagnostics/QcCrashHandler.h diagnostics/QcCrashHandler.cpp
    diagnostics/QcDiagnosticsBundle.h diagnostics/QcDiagnosticsBundle.cpp
    diagnostics/QcDiagnosticsOptions.h diagnostics/QcDiagnosticsOptions.cpp
    diagnostics/QcDiagnosticsSession.h diagnostics/QcDiagnosticsSession.cpp
    diagnostics/QcHealthLog.h diagnostics/QcHealthLog.cpp
    diagnostics/QcHealthSample.h
    diagnostics/QcIntervalStats.h diagnostics/QcIntervalStats.cpp
    diagnostics/QcLogFiles.h diagnostics/QcLogFiles.cpp
    diagnostics/QcLogWriter.h diagnostics/QcLogWriter.cpp
    diagnostics/QcMessageLog.h diagnostics/QcMessageLog.cpp
    diagnostics/QcRoundTripStats.h diagnostics/QcRoundTripStats.cpp
    diagnostics/QcSignalDefault.h
    diagnostics/QcThreadCpuClock.h diagnostics/QcThreadCpuClock.cpp
)
target_sources(qc_common PRIVATE
    gameplay/QcAdvancementProgress.h gameplay/QcAdvancementProgress.cpp
    gameplay/QcAdventureRules.h gameplay/QcAdventureRules.cpp
    gameplay/QcAnvil.h gameplay/QcAnvil.cpp
    gameplay/QcArmour.h gameplay/QcArmour.cpp
    gameplay/QcCrafting.h gameplay/QcCrafting.cpp
    gameplay/QcCreativeCatalog.h gameplay/QcCreativeCatalog.cpp
    gameplay/QcEnchanting.h gameplay/QcEnchanting.cpp
    gameplay/QcEnchantingTable.h gameplay/QcEnchantingTable.cpp
    gameplay/QcEquipment.h gameplay/QcEquipment.cpp
    gameplay/QcExperience.h gameplay/QcExperience.cpp
    gameplay/QcGameMode.h gameplay/QcGameMode.cpp
    gameplay/QcHanging.h gameplay/QcHanging.cpp
    gameplay/QcInventory.h gameplay/QcInventory.cpp
    gameplay/QcItemStack.h gameplay/QcItemStack.cpp
    gameplay/QcKeyBindings.h gameplay/QcKeyBindings.cpp
    gameplay/QcMining.h gameplay/QcMining.cpp
    gameplay/QcPortableContainers.h gameplay/QcPortableContainers.cpp
    gameplay/QcQuickMove.h gameplay/QcQuickMove.cpp
    gameplay/QcRepair.h gameplay/QcRepair.cpp
    gameplay/QcSlotList.h gameplay/QcSlotList.cpp
    gameplay/QcStandPose.h gameplay/QcStandPose.cpp
    gameplay/QcStatusEffects.h gameplay/QcStatusEffects.cpp
    gameplay/QcSwingSpeed.h gameplay/QcSwingSpeed.cpp
)
target_sources(qc_common PRIVATE
    handbook/QcHandbook.h handbook/QcHandbook.cpp
    handbook/QcHandbookChapter.h
    handbook/QcHandbookEntry.h
    handbook/QcHandbookJson.h handbook/QcHandbookJson.cpp
    handbook/QcHandbookLinks.h handbook/QcHandbookLinks.cpp
    handbook/QcHandbookMarkdown.h handbook/QcHandbookMarkdown.cpp
    handbook/QcHandbookWeb.h handbook/QcHandbookWeb.cpp
    handbook/QcLinkList.h handbook/QcLinkList.cpp
)
target_sources(qc_common PRIVATE
    handbook/builder/QcHandbookBlockPages.h handbook/builder/QcHandbookBlockPages.cpp
    handbook/builder/QcHandbookBlockTables.h handbook/builder/QcHandbookBlockTables.cpp
    handbook/builder/QcHandbookBuilder.h handbook/builder/QcHandbookBuilder.cpp
    handbook/builder/QcHandbookChapterPages.h handbook/builder/QcHandbookChapterPages.cpp
    handbook/builder/QcHandbookContext.h
    handbook/builder/QcHandbookCreaturePages.h handbook/builder/QcHandbookCreaturePages.cpp
    handbook/builder/QcHandbookIndex.h handbook/builder/QcHandbookIndex.cpp
    handbook/builder/QcHandbookItemPages.h handbook/builder/QcHandbookItemPages.cpp
    handbook/builder/QcHandbookOverviewPages.h handbook/builder/QcHandbookOverviewPages.cpp
    handbook/builder/QcHandbookRecipes.h handbook/builder/QcHandbookRecipes.cpp
    handbook/builder/QcHandbookText.h handbook/builder/QcHandbookText.cpp
    handbook/builder/QcHandbookWorldPages.h handbook/builder/QcHandbookWorldPages.cpp
)
target_sources(qc_common PRIVATE
    light/QcLightCells.h
    light/QcLightEngine.h light/QcLightEngine.cpp
    light/QcLightNibbles.h
    light/QcLightTable.h light/QcLightTable.cpp
    light/QcSkyLight.h light/QcSkyLight.cpp
)
target_sources(qc_common PRIVATE
    maps/QcMapCrafting.h maps/QcMapCrafting.cpp
    maps/QcMapData.h maps/QcMapData.cpp
    maps/QcMapMarker.h
    maps/QcMapPixel.h
)
target_sources(qc_common PRIVATE
    physics/QcBoatMotion.h physics/QcBoatMotion.cpp
    physics/QcBodyFit.h physics/QcBodyFit.cpp
    physics/QcBodyGrid.h physics/QcBodyGrid.cpp
    physics/QcBodyPush.h physics/QcBodyPush.cpp
    physics/QcEntityMotion.h
    physics/QcPhysics.h physics/QcPhysics.cpp
    physics/QcPlayerInput.h
    physics/QcPose.h physics/QcPose.cpp
    physics/QcRaycast.h physics/QcRaycast.cpp
    physics/QcSpawnGround.h physics/QcSpawnGround.cpp
)
target_sources(qc_common PRIVATE
    protocol/QcAdminCodec.cpp
    protocol/QcAdminMessages.h
    protocol/QcAdminSamples.h protocol/QcAdminSamples.cpp
    protocol/QcAdvancementCodec.cpp
    protocol/QcAdvancementMessages.h
    protocol/QcChatText.h protocol/QcChatText.cpp
    protocol/QcChunkCodec.cpp
    protocol/QcConnection.h protocol/QcConnection.cpp
    protocol/QcDebugCodec.cpp
    protocol/QcDebugMessages.h
    protocol/QcEntityCodec.cpp
    protocol/QcEntityMessages.h
    protocol/QcFrameReader.h protocol/QcFrameReader.cpp
    protocol/QcInProcessTransport.h protocol/QcInProcessTransport.cpp
    protocol/QcLoginCodec.cpp
    protocol/QcLoginMessages.h
    protocol/QcLoginSettings.h
    protocol/QcMachineCodec.cpp
    protocol/QcMachineMessages.h
    protocol/QcMapCodec.cpp
    protocol/QcMapMessages.h
    protocol/QcMessages.h
    protocol/QcPackComparison.h protocol/QcPackComparison.cpp
    protocol/QcPlayerCodec.cpp
    protocol/QcPlayerMessages.h
    protocol/QcProtocol.h protocol/QcProtocol.cpp
    protocol/QcProtocolCodec.h protocol/QcProtocolCodec.cpp
    protocol/QcSessionCodec.cpp
    protocol/QcSessionMessages.h
    protocol/QcSignCodec.cpp
    protocol/QcSignMessages.h
    protocol/QcStreamLimits.h
    protocol/QcTradeCodec.cpp
    protocol/QcTradeMessages.h
    protocol/QcTransport.h
    protocol/QcWorldCodec.cpp
    protocol/QcWorldMessages.h
)
target_sources(qc_common PRIVATE
    world/QcBlockAccess.h
    world/QcBoatBody.h
    world/QcChunk.h world/QcChunk.cpp
    world/QcChunkSection.h world/QcChunkSection.cpp
    world/QcDamagedFile.h
    world/QcDayCycle.h world/QcDayCycle.cpp
    world/QcEntityKind.h world/QcEntityKind.cpp
    world/QcGeneratorVersion.h
    world/QcLightPassage.h
    world/QcLightVolume.h world/QcLightVolume.cpp
    world/QcMinecartBody.h
    world/QcPlacedChunkLoader.h
    world/QcPrecipitationTable.h world/QcPrecipitationTable.cpp
    world/QcSignText.h world/QcSignText.cpp
    world/QcVegetationTable.h world/QcVegetationTable.cpp
    world/QcWeather.h world/QcWeather.cpp
    world/QcWorldInfo.h world/QcWorldInfo.cpp
    world/QcWorldLabel.h world/QcWorldLabel.cpp
)
qc_configure_target(qc_common)
# Movement as alike as the compilers allow (decision E-901): no fused multiply-add contraction in
# the physics, so a server on one platform and a client predicting on another round alike in
# practice (std::sin, std::cos and std::hypot still come from each platform's library; the server
# decides). Every source in physics/.
get_target_property(qc_common_sources qc_common SOURCES)
list(FILTER qc_common_sources INCLUDE REGEX "^physics/.*\\.cpp$")
set_source_files_properties(${qc_common_sources} PROPERTIES COMPILE_OPTIONS "${QC_NO_FP_CONTRACT}")
# Includes name the header by its path below src/ ("common/blockrules/QcRails.h", decision E-418):
# src/ is the only include directory a library passes on.
target_include_directories(qc_common PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(qc_common PUBLIC Qt::Core)

# The crash handler's platform part (decision E-834): minidumps with dbghelp on Windows, signals
# elsewhere.
if(WIN32)
    target_sources(qc_common PRIVATE diagnostics/QcCrashHandler_win.cpp)
    target_link_libraries(qc_common PRIVATE dbghelp psapi)
else()
    target_sources(qc_common PRIVATE diagnostics/QcCrashHandler_posix.cpp)
endif()

# Version and git commit of the build, for this one file only (decision E-834): taken at configure
# time, so a new commit does not rebuild the libraries; a fresh configure (release, Windows build)
# brings the current commit.
find_package(Git QUIET)
set(QC_BUILD_COMMIT "unknown")
if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short=10 HEAD WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
                    OUTPUT_VARIABLE QC_GIT_HEAD OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE QC_GIT_RESULT)
    if(QC_GIT_RESULT EQUAL 0 AND QC_GIT_HEAD)
        set(QC_BUILD_COMMIT "${QC_GIT_HEAD}")
        execute_process(COMMAND ${GIT_EXECUTABLE} status --porcelain --untracked-files=no WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
                        OUTPUT_VARIABLE QC_GIT_CHANGES OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(QC_GIT_CHANGES)
            string(APPEND QC_BUILD_COMMIT "-dirty")
        endif()
    endif()
endif()
set_source_files_properties(diagnostics/QcBuildInfo.cpp PROPERTIES
    COMPILE_DEFINITIONS "QC_BUILD_VERSION=\"${PROJECT_VERSION}\";QC_BUILD_COMMIT=\"${QC_BUILD_COMMIT}\"")

# The loader of the base game content (:/content/qtcraft, content/CMakeLists.txt) is part of
# qc_base_content, so only the targets that load the base content embed it (decision E-419): the
# server, the headless program and the tests that load it.
target_sources(qc_base_content PRIVATE
    content/loader/QcBaseContent.h content/loader/QcBaseContent.cpp
)
target_link_libraries(qc_base_content PUBLIC qc_common)
