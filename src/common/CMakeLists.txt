qt_add_library(qc_common STATIC)
# Sources per folder, sorted, each header next to its sources (decision E-417;
# scripts/check-cmake-lists.py checks the lists).
target_sources(qc_common PRIVATE
    auth/QcLoginCrypto.h auth/QcLoginCrypto.cpp
    auth/QcPlayerKey.h auth/QcPlayerKey.cpp
    auth/QcProfileValidation.h auth/QcProfileValidation.cpp
)
target_sources(qc_common PRIVATE
    blockrules/QcBlockRules.h blockrules/QcBlockRules.cpp
    blockrules/QcRails.h blockrules/QcRails.cpp
    blockrules/QcSlabRules.h blockrules/QcSlabRules.cpp
    blockrules/QcStairShapes.h blockrules/QcStairShapes.cpp
)
target_sources(qc_common PRIVATE
    content/definitions/QcAppearance.h
    content/definitions/QcAppearanceDefinition.h
    content/definitions/QcArmourDefinition.h
    content/definitions/QcBlastDefinition.h
    content/definitions/QcBlockDefinition.h
    content/definitions/QcBlockProperty.h
    content/definitions/QcCoastalTowerDefinition.h
    content/definitions/QcCommandMacroDefinition.h
    content/definitions/QcCookingDefinitions.h
    content/definitions/QcDungeonDefinition.h
    content/definitions/QcEffectDefinition.h
    content/definitions/QcExperienceDrop.h
    content/definitions/QcFarmingDefinitions.h
    content/definitions/QcFireDefinition.h
    content/definitions/QcHangingDefinition.h
    content/definitions/QcHitEffect.h
    content/definitions/QcHoldDisplay.h content/definitions/QcHoldDisplay.cpp
    content/definitions/QcItemDefinition.h content/definitions/QcItemDefinition.cpp
    content/definitions/QcLiquidDefinition.h
    content/definitions/QcLiquidReaction.h
    content/definitions/QcLootTableDefinition.h
    content/definitions/QcMachineDefinition.h
    content/definitions/QcMaterialDefinition.h
    content/definitions/QcMineshaftDefinition.h
    content/definitions/QcMobDefinition.h
    content/definitions/QcMobTraits.h
    content/definitions/QcModelDefinition.h
    content/definitions/QcPackReference.h
    content/definitions/QcPartFamily.h
    content/definitions/QcProjectileDefinition.h
    content/definitions/QcRecipeDefinition.h content/definitions/QcRecipeDefinition.cpp
    content/definitions/QcShapeCondition.h
    content/definitions/QcShapeDefinition.h
    content/definitions/QcShapePart.h
    content/definitions/QcShowcaseDefinition.h
    content/definitions/QcSignalDefinition.h
    content/definitions/QcSmeltingRecipe.h
    content/definitions/QcSoundDefinition.h content/definitions/QcSoundDefinition.cpp
    content/definitions/QcSoundKeys.h content/definitions/QcSoundKeys.cpp
    content/definitions/QcSpawnerDefinition.h
    content/definitions/QcStructureDefinition.h
    content/definitions/QcTextureDefinition.h
    content/definitions/QcTradeDefinitions.h
    content/definitions/QcVegetationDefinition.h
    content/definitions/QcVillageDefinitions.h
    content/definitions/QcVillagerDefinitions.h
    content/definitions/QcWeatherDefinition.h
    content/definitions/QcWorldgenDefinitions.h
    content/definitions/QcWorldTypeDefinitions.h
)
target_sources(qc_common PRIVATE
    content/loader/QcContentPackLoader.h content/loader/QcContentPackLoader.cpp
)
target_sources(qc_common PRIVATE
    content/parser/QcArmourParser.h content/parser/QcArmourParser.cpp
    content/parser/QcBlastParser.h content/parser/QcBlastParser.cpp
    content/parser/QcBlockParser.h content/parser/QcBlockParser.cpp
    content/parser/QcCoastalTowerParser.h content/parser/QcCoastalTowerParser.cpp
    content/parser/QcCommandMacroParser.h content/parser/QcCommandMacroParser.cpp
    content/parser/QcContentFields.h content/parser/QcContentFields.cpp
    content/parser/QcContentParser.h content/parser/QcContentParser.cpp
    content/parser/QcCookingParser.h content/parser/QcCookingParser.cpp
    content/parser/QcEffectParser.h content/parser/QcEffectParser.cpp
    content/parser/QcFarmingParser.h content/parser/QcFarmingParser.cpp
    content/parser/QcHangingParser.h content/parser/QcHangingParser.cpp
    content/parser/QcHoldParser.h content/parser/QcHoldParser.cpp
    content/parser/QcItemParser.h content/parser/QcItemParser.cpp
    content/parser/QcJsonFieldReader.h content/parser/QcJsonFieldReader.cpp
    content/parser/QcLiquidParser.h content/parser/QcLiquidParser.cpp
    content/parser/QcLootTableParser.h content/parser/QcLootTableParser.cpp
    content/parser/QcMaterialParser.h content/parser/QcMaterialParser.cpp
    content/parser/QcMobParser.h content/parser/QcMobParser.cpp
    content/parser/QcMobReader.h content/parser/QcMobReader.cpp content/parser/QcMobReaderTraits.cpp
    content/parser/QcProjectileParser.h content/parser/QcProjectileParser.cpp
    content/parser/QcRailParser.h content/parser/QcRailParser.cpp
    content/parser/QcRecipeParser.h content/parser/QcRecipeParser.cpp
    content/parser/QcShapeParser.h content/parser/QcShapeParser.cpp
    content/parser/QcShowcaseParser.h content/parser/QcShowcaseParser.cpp
    content/parser/QcSignalParser.h content/parser/QcSignalParser.cpp
    content/parser/QcSoundParser.h content/parser/QcSoundParser.cpp
    content/parser/QcStructureParser.h content/parser/QcStructureParser.cpp
    content/parser/QcStructureSiteParser.h content/parser/QcStructureSiteParser.cpp
    content/parser/QcStructureVariationParser.h content/parser/QcStructureVariationParser.cpp
    content/parser/QcTradeParser.h content/parser/QcTradeParser.cpp
    content/parser/QcVegetationParser.h content/parser/QcVegetationParser.cpp
    content/parser/QcVillageLayoutParser.h content/parser/QcVillageLayoutParser.cpp
    content/parser/QcVillageParser.h content/parser/QcVillageParser.cpp
    content/parser/QcVillagerParser.h content/parser/QcVillagerParser.cpp
    content/parser/QcWeatherParser.h content/parser/QcWeatherParser.cpp
    content/parser/QcWorldgenParser.h content/parser/QcWorldgenParser.cpp
    content/parser/QcWorldTypeParser.h content/parser/QcWorldTypeParser.cpp
)
target_sources(qc_common PRIVATE
    content/registry/QcArmourSerializer.h content/registry/QcArmourSerializer.cpp
    content/registry/QcAssetCatalog.h
    content/registry/QcBlockSerializer.h content/registry/QcBlockSerializer.cpp
    content/registry/QcBlockState.h
    content/registry/QcBlockStateTable.h content/registry/QcBlockStateTable.cpp
    content/registry/QcContentNames.h content/registry/QcContentNames.cpp
    content/registry/QcContentRegistry.h content/registry/QcContentRegistry.cpp
    content/registry/QcContentSerializer.h content/registry/QcContentSerializer.cpp
    content/registry/QcHoldSerializer.h content/registry/QcHoldSerializer.cpp
    content/registry/QcItemSerializer.h content/registry/QcItemSerializer.cpp
    content/registry/QcLootTables.h content/registry/QcLootTables.cpp
    content/registry/QcMobSerializer.h content/registry/QcMobSerializer.cpp
    content/registry/QcMobTraitsContent.h content/registry/QcMobTraitsContent.cpp
    content/registry/QcNamedList.h
    content/registry/QcPartGenerator.h content/registry/QcPartGenerator.cpp
    content/registry/QcSignalLinks.h content/registry/QcSignalLinks.cpp
    content/registry/QcSpawnerContent.h content/registry/QcSpawnerContent.cpp
    content/registry/QcSpawnWeights.h content/registry/QcSpawnWeights.cpp
    content/registry/QcStonecutting.h content/registry/QcStonecutting.cpp
    content/registry/QcTranslations.h content/registry/QcTranslations.cpp
    content/registry/QcVillageCatalog.h content/registry/QcVillageCatalog.cpp
    content/registry/QcVillageContent.h content/registry/QcVillageContent.cpp
    content/registry/QcVillagerOutfits.h content/registry/QcVillagerOutfits.cpp
    content/registry/QcWorldgenCatalog.h
)
target_sources(qc_common PRIVATE
    core/QcAabb.h
    core/QcBlockFace.h
    core/QcBlockPos.h core/QcBlockPos.cpp
    core/QcChunkPos.h core/QcChunkPos.cpp
    core/QcCommandSyntax.h core/QcCommandSyntax.cpp
    core/QcCrc32.h core/QcCrc32.cpp
    core/QcDataRoot.h core/QcDataRoot.cpp
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
)
target_sources(qc_common PRIVATE
    gameplay/QcAdventureRules.h gameplay/QcAdventureRules.cpp
    gameplay/QcArmour.h gameplay/QcArmour.cpp
    gameplay/QcCrafting.h gameplay/QcCrafting.cpp
    gameplay/QcCreativeCatalog.h gameplay/QcCreativeCatalog.cpp
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
    physics/QcBodyFit.h physics/QcBodyFit.cpp
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
    world/QcChunk.h world/QcChunk.cpp
    world/QcChunkSection.h world/QcChunkSection.cpp
    world/QcDamagedFile.h
    world/QcDayCycle.h world/QcDayCycle.cpp
    world/QcEntityKind.h world/QcEntityKind.cpp
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
