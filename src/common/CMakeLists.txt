qt_add_library(qc_common STATIC)
# Sources per folder, sorted, each header next to its sources (decision E-417;
# scripts/check-cmake-lists.py checks the lists).
target_sources(qc_common PRIVATE
    auth/QcLoginCrypto.h auth/QcLoginCrypto.cpp
    auth/QcPlayerKey.h auth/QcPlayerKey.cpp
)
target_sources(qc_common PRIVATE
    content/definitions/QcAppearance.h
    content/definitions/QcAppearanceDefinition.h
    content/definitions/QcArmourDefinition.h
    content/definitions/QcBlockDefinition.h
    content/definitions/QcBlockProperty.h
    content/definitions/QcCommandMacroDefinition.h
    content/definitions/QcExperienceDrop.h
    content/definitions/QcFarmingDefinitions.h
    content/definitions/QcFireDefinition.h
    content/definitions/QcItemDefinition.h
    content/definitions/QcLiquidDefinition.h
    content/definitions/QcLiquidReaction.h
    content/definitions/QcMachineDefinition.h
    content/definitions/QcMaterialDefinition.h
    content/definitions/QcMobDefinition.h
    content/definitions/QcModelDefinition.h
    content/definitions/QcPackReference.h
    content/definitions/QcPartFamily.h
    content/definitions/QcRecipeDefinition.h content/definitions/QcRecipeDefinition.cpp
    content/definitions/QcShapeCondition.h
    content/definitions/QcShapeDefinition.h
    content/definitions/QcShapePart.h
    content/definitions/QcShowcaseDefinition.h
    content/definitions/QcSignalDefinition.h
    content/definitions/QcSmeltingRecipe.h
    content/definitions/QcSoundDefinition.h content/definitions/QcSoundDefinition.cpp
    content/definitions/QcSoundKeys.h content/definitions/QcSoundKeys.cpp
    content/definitions/QcStructureDefinition.h
    content/definitions/QcTextureDefinition.h
    content/definitions/QcTradeDefinitions.h
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
    content/parser/QcBlockParser.h content/parser/QcBlockParser.cpp
    content/parser/QcCommandMacroParser.h content/parser/QcCommandMacroParser.cpp
    content/parser/QcContentFields.h content/parser/QcContentFields.cpp
    content/parser/QcContentParser.h content/parser/QcContentParser.cpp
    content/parser/QcFarmingParser.h content/parser/QcFarmingParser.cpp
    content/parser/QcItemParser.h content/parser/QcItemParser.cpp
    content/parser/QcJsonFieldReader.h content/parser/QcJsonFieldReader.cpp
    content/parser/QcMaterialParser.h content/parser/QcMaterialParser.cpp
    content/parser/QcMobParser.h content/parser/QcMobParser.cpp
    content/parser/QcRecipeParser.h content/parser/QcRecipeParser.cpp
    content/parser/QcShapeParser.h content/parser/QcShapeParser.cpp
    content/parser/QcShowcaseParser.h content/parser/QcShowcaseParser.cpp
    content/parser/QcSignalParser.h content/parser/QcSignalParser.cpp
    content/parser/QcSoundParser.h content/parser/QcSoundParser.cpp
    content/parser/QcStructureParser.h content/parser/QcStructureParser.cpp
    content/parser/QcStructureVariationParser.h content/parser/QcStructureVariationParser.cpp
    content/parser/QcTradeParser.h content/parser/QcTradeParser.cpp
    content/parser/QcVillageLayoutParser.h content/parser/QcVillageLayoutParser.cpp
    content/parser/QcVillageParser.h content/parser/QcVillageParser.cpp
    content/parser/QcVillagerParser.h content/parser/QcVillagerParser.cpp
    content/parser/QcWeatherParser.h content/parser/QcWeatherParser.cpp
    content/parser/QcWorldgenParser.h content/parser/QcWorldgenParser.cpp
    content/parser/QcWorldTypeParser.h content/parser/QcWorldTypeParser.cpp
)
target_sources(qc_common PRIVATE
    content/registry/QcArmourSerializer.h content/registry/QcArmourSerializer.cpp
    content/registry/QcBlockSerializer.h content/registry/QcBlockSerializer.cpp
    content/registry/QcBlockState.h
    content/registry/QcBlockStateTable.h content/registry/QcBlockStateTable.cpp
    content/registry/QcContentNames.h content/registry/QcContentNames.cpp
    content/registry/QcContentRegistry.h content/registry/QcContentRegistry.cpp
    content/registry/QcContentSerializer.h content/registry/QcContentSerializer.cpp
    content/registry/QcPartGenerator.h content/registry/QcPartGenerator.cpp
    content/registry/QcSignalLinks.h content/registry/QcSignalLinks.cpp
    content/registry/QcVillageContent.h content/registry/QcVillageContent.cpp
    content/registry/QcVillagerOutfits.h content/registry/QcVillagerOutfits.cpp
)
target_sources(qc_common PRIVATE
    core/QcAabb.h
    core/QcBlockFace.h
    core/QcBlockPos.h core/QcBlockPos.cpp
    core/QcChunkPos.h core/QcChunkPos.cpp
    core/QcCrc32.h core/QcCrc32.cpp
    core/QcDataRoot.h core/QcDataRoot.cpp
    core/QcIntMath.h
    core/QcMailbox.h
    core/QcNameText.h core/QcNameText.cpp
    core/QcPhaseWindow.h core/QcPhaseWindow.cpp
    core/QcProcessMemory.h core/QcProcessMemory.cpp
    core/QcResult.h
    core/QcSectionKey.h
    core/QcTickPhase.h
    core/QcVec3.h
    core/QcWorkerPool.h core/QcWorkerPool.cpp
    core/QcWorldConstants.h
)
target_sources(qc_common PRIVATE
    gameplay/QcArmour.h gameplay/QcArmour.cpp
    gameplay/QcCrafting.h gameplay/QcCrafting.cpp
    gameplay/QcCreativeCatalog.h gameplay/QcCreativeCatalog.cpp
    gameplay/QcEquipment.h gameplay/QcEquipment.cpp
    gameplay/QcExperience.h gameplay/QcExperience.cpp
    gameplay/QcGameMode.h gameplay/QcGameMode.cpp
    gameplay/QcInventory.h gameplay/QcInventory.cpp
    gameplay/QcItemStack.h
    gameplay/QcKeyBindings.h gameplay/QcKeyBindings.cpp
    gameplay/QcMining.h gameplay/QcMining.cpp
    gameplay/QcQuickMove.h gameplay/QcQuickMove.cpp
    gameplay/QcRepair.h gameplay/QcRepair.cpp
    gameplay/QcStandPose.h gameplay/QcStandPose.cpp
)
target_sources(qc_common PRIVATE
    handbook/QcHandbook.h handbook/QcHandbook.cpp
    handbook/QcHandbookChapter.h
    handbook/QcHandbookEntry.h
    handbook/QcHandbookJson.h handbook/QcHandbookJson.cpp
    handbook/QcHandbookLinks.h handbook/QcHandbookLinks.cpp
    handbook/QcHandbookMarkdown.h handbook/QcHandbookMarkdown.cpp
    handbook/QcHandbookWeb.h handbook/QcHandbookWeb.cpp
)
target_sources(qc_common PRIVATE
    handbook/builder/QcHandbookBlockPages.h handbook/builder/QcHandbookBlockPages.cpp
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
    light/QcLightEngine.h light/QcLightEngine.cpp
    light/QcLightNibbles.h
    light/QcLightTable.h light/QcLightTable.cpp
)
target_sources(qc_common PRIVATE
    physics/QcBodyFit.h physics/QcBodyFit.cpp
    physics/QcEntityMotion.h
    physics/QcPhysics.h physics/QcPhysics.cpp
    physics/QcPlayerInput.h
    physics/QcPose.h physics/QcPose.cpp
    physics/QcRaycast.h physics/QcRaycast.cpp
)
target_sources(qc_common PRIVATE
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
    protocol/QcMachineCodec.cpp
    protocol/QcMachineMessages.h
    protocol/QcMessages.h
    protocol/QcPackComparison.h protocol/QcPackComparison.cpp
    protocol/QcPlayerCodec.cpp
    protocol/QcPlayerMessages.h
    protocol/QcProtocol.h protocol/QcProtocol.cpp
    protocol/QcProtocolCodec.h protocol/QcProtocolCodec.cpp
    protocol/QcSessionCodec.cpp
    protocol/QcSessionMessages.h
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
    world/QcDayCycle.h world/QcDayCycle.cpp
    world/QcEntityKind.h world/QcEntityKind.cpp
    world/QcLightPassage.h
    world/QcLightVolume.h world/QcLightVolume.cpp
    world/QcMinecartBody.h
    world/QcPrecipitationTable.h world/QcPrecipitationTable.cpp
    world/QcWeather.h world/QcWeather.cpp
    world/QcWorldLabel.h world/QcWorldLabel.cpp
)
qc_configure_target(qc_common)
# Includes name the header by its path below src/ ("server/blocks/QcRails.h", decision E-418):
# src/ is the only include directory a library passes on.
target_include_directories(qc_common PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(qc_common PUBLIC Qt::Core)

# The loader of the base game content (:/content/qtcraft, content/CMakeLists.txt) is part of
# qc_base_content, so only the targets that load the base content embed it (decision E-419): the
# server, the headless program and the tests that load it.
target_sources(qc_base_content PRIVATE
    content/loader/QcBaseContent.h content/loader/QcBaseContent.cpp
)
target_link_libraries(qc_base_content PUBLIC qc_common)
