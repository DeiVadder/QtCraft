qt_add_library(qc_common STATIC
    core/QcAabb.h
    world/QcBlockAccess.h
    content/definitions/QcBlockDefinition.h
    content/definitions/QcBlockProperty.h
    content/registry/QcBlockState.h
    content/registry/QcBlockStateTable.h content/registry/QcBlockStateTable.cpp
    content/definitions/QcShapeCondition.h
    content/definitions/QcShapeDefinition.h
    content/definitions/QcShapePart.h
    world/QcLightPassage.h
    world/QcLightVolume.h world/QcLightVolume.cpp
    core/QcBlockFace.h
    core/QcBlockPos.h core/QcBlockPos.cpp
    world/QcChunk.h world/QcChunk.cpp
    core/QcChunkPos.h core/QcChunkPos.cpp
    world/QcChunkSection.h world/QcChunkSection.cpp
    protocol/QcChatText.h protocol/QcChatText.cpp
    protocol/QcConnection.h protocol/QcConnection.cpp
    protocol/QcDebugMessages.h protocol/QcDebugCodec.cpp
    content/registry/QcContentNames.h content/registry/QcContentNames.cpp
    core/QcCrc32.h core/QcCrc32.cpp
    core/QcDataRoot.h core/QcDataRoot.cpp
    auth/QcLoginCrypto.h auth/QcLoginCrypto.cpp
    auth/QcPlayerKey.h auth/QcPlayerKey.cpp
    core/QcNameText.h core/QcNameText.cpp
    core/QcPhaseWindow.h core/QcPhaseWindow.cpp
    core/QcProcessMemory.h core/QcProcessMemory.cpp
    core/QcTickPhase.h
    world/QcDayCycle.h world/QcDayCycle.cpp
    world/QcWorldLabel.h world/QcWorldLabel.cpp
    content/parser/QcBlockParser.h content/parser/QcBlockParser.cpp
    content/parser/QcCommandMacroParser.h content/parser/QcCommandMacroParser.cpp
    content/definitions/QcCommandMacroDefinition.h
    content/parser/QcContentFields.h content/parser/QcContentFields.cpp
    content/parser/QcContentParser.h content/parser/QcContentParser.cpp
    content/parser/QcArmourParser.h content/parser/QcArmourParser.cpp
    content/parser/QcItemParser.h content/parser/QcItemParser.cpp
    content/parser/QcJsonFieldReader.h content/parser/QcJsonFieldReader.cpp
    content/parser/QcMaterialParser.h content/parser/QcMaterialParser.cpp
    content/parser/QcMobParser.h content/parser/QcMobParser.cpp
    content/parser/QcRecipeParser.h content/parser/QcRecipeParser.cpp
    content/parser/QcFarmingParser.h content/parser/QcFarmingParser.cpp
    content/parser/QcShapeParser.h content/parser/QcShapeParser.cpp
    content/parser/QcSignalParser.h content/parser/QcSignalParser.cpp
    content/definitions/QcSignalDefinition.h
    content/parser/QcSoundParser.h content/parser/QcSoundParser.cpp
    content/parser/QcShowcaseParser.h content/parser/QcShowcaseParser.cpp
    content/parser/QcStructureParser.h content/parser/QcStructureParser.cpp
    content/definitions/QcShowcaseDefinition.h
    content/definitions/QcStructureDefinition.h
    content/definitions/QcWorldTypeDefinitions.h
    content/parser/QcWorldTypeParser.h content/parser/QcWorldTypeParser.cpp
    content/parser/QcWorldgenParser.h content/parser/QcWorldgenParser.cpp
    content/registry/QcContentRegistry.h content/registry/QcContentRegistry.cpp
    content/registry/QcArmourSerializer.h content/registry/QcArmourSerializer.cpp
    content/registry/QcBlockSerializer.h content/registry/QcBlockSerializer.cpp
    content/registry/QcContentSerializer.h content/registry/QcContentSerializer.cpp
    content/definitions/QcPackReference.h
    content/definitions/QcPartFamily.h
    content/registry/QcPartGenerator.h content/registry/QcPartGenerator.cpp
    content/registry/QcSignalLinks.h content/registry/QcSignalLinks.cpp
    content/definitions/QcArmourDefinition.h
    content/definitions/QcMaterialDefinition.h
    gameplay/QcCrafting.h gameplay/QcCrafting.cpp
    gameplay/QcCreativeCatalog.h gameplay/QcCreativeCatalog.cpp
    physics/QcEntityMotion.h
    handbook/QcHandbook.h handbook/QcHandbook.cpp
    handbook/builder/QcHandbookBlockPages.h handbook/builder/QcHandbookBlockPages.cpp
    handbook/builder/QcHandbookBuilder.h handbook/builder/QcHandbookBuilder.cpp
    handbook/QcHandbookChapter.h
    handbook/builder/QcHandbookChapterPages.h handbook/builder/QcHandbookChapterPages.cpp
    handbook/builder/QcHandbookContext.h
    handbook/builder/QcHandbookCreaturePages.h handbook/builder/QcHandbookCreaturePages.cpp
    handbook/QcHandbookEntry.h
    handbook/builder/QcHandbookIndex.h handbook/builder/QcHandbookIndex.cpp
    handbook/builder/QcHandbookItemPages.h handbook/builder/QcHandbookItemPages.cpp
    handbook/QcHandbookJson.h handbook/QcHandbookJson.cpp
    handbook/QcHandbookLinks.h handbook/QcHandbookLinks.cpp
    handbook/QcHandbookMarkdown.h handbook/QcHandbookMarkdown.cpp
    handbook/builder/QcHandbookOverviewPages.h handbook/builder/QcHandbookOverviewPages.cpp
    handbook/builder/QcHandbookRecipes.h handbook/builder/QcHandbookRecipes.cpp
    handbook/builder/QcHandbookText.h handbook/builder/QcHandbookText.cpp
    handbook/builder/QcHandbookWorldPages.h handbook/builder/QcHandbookWorldPages.cpp
    protocol/QcInProcessTransport.h protocol/QcInProcessTransport.cpp
    core/QcIntMath.h
    gameplay/QcArmour.h gameplay/QcArmour.cpp
    gameplay/QcEquipment.h gameplay/QcEquipment.cpp
    gameplay/QcGameMode.h gameplay/QcGameMode.cpp
    gameplay/QcRepair.h gameplay/QcRepair.cpp
    gameplay/QcStandPose.h gameplay/QcStandPose.cpp
    gameplay/QcInventory.h gameplay/QcInventory.cpp
    gameplay/QcQuickMove.h gameplay/QcQuickMove.cpp
    content/definitions/QcItemDefinition.h
    gameplay/QcItemStack.h
    gameplay/QcKeyBindings.h gameplay/QcKeyBindings.cpp
    content/definitions/QcFarmingDefinitions.h
    content/definitions/QcLiquidDefinition.h
    content/definitions/QcLiquidReaction.h
    content/definitions/QcMachineDefinition.h
    core/QcMailbox.h
    protocol/QcMessages.h
    protocol/QcEntityMessages.h
    protocol/QcMachineMessages.h
    protocol/QcPlayerMessages.h
    protocol/QcSessionMessages.h
    protocol/QcWorldMessages.h
    gameplay/QcMining.h gameplay/QcMining.cpp
    content/definitions/QcAppearance.h
    content/definitions/QcAppearanceDefinition.h
    content/definitions/QcMobDefinition.h
    content/definitions/QcModelDefinition.h
    physics/QcPhysics.h physics/QcPhysics.cpp
    physics/QcPlayerInput.h
    protocol/QcProtocol.h protocol/QcProtocol.cpp
    protocol/QcProtocolCodec.h protocol/QcProtocolCodec.cpp
    protocol/QcChunkCodec.cpp
    protocol/QcSessionCodec.cpp
    protocol/QcWorldCodec.cpp
    protocol/QcPlayerCodec.cpp
    protocol/QcEntityCodec.cpp
    protocol/QcMachineCodec.cpp
    protocol/QcLoginMessages.h
    protocol/QcLoginCodec.cpp
    protocol/QcFrameReader.h protocol/QcFrameReader.cpp
    protocol/QcPackComparison.h protocol/QcPackComparison.cpp
    physics/QcRaycast.h physics/QcRaycast.cpp
    content/definitions/QcRecipeDefinition.h content/definitions/QcRecipeDefinition.cpp
    core/QcResult.h
    content/definitions/QcSmeltingRecipe.h
    content/definitions/QcSoundDefinition.h content/definitions/QcSoundDefinition.cpp
    content/definitions/QcSoundKeys.h content/definitions/QcSoundKeys.cpp
    protocol/QcStreamLimits.h
    content/definitions/QcTextureDefinition.h
    protocol/QcTransport.h
    core/QcVec3.h
    content/definitions/QcWorldgenDefinitions.h
    core/QcWorkerPool.h core/QcWorkerPool.cpp
    core/QcWorldConstants.h
)
qc_configure_target(qc_common)
# Classes live in thematic subfolders (E-089); every folder is an include directory, so includes
# stay "QcName.h" without a path.
target_include_directories(qc_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/auth
    ${CMAKE_CURRENT_SOURCE_DIR}/world
    ${CMAKE_CURRENT_SOURCE_DIR}/physics
    ${CMAKE_CURRENT_SOURCE_DIR}/gameplay
    ${CMAKE_CURRENT_SOURCE_DIR}/protocol
    ${CMAKE_CURRENT_SOURCE_DIR}/content/definitions
    ${CMAKE_CURRENT_SOURCE_DIR}/content/registry
    ${CMAKE_CURRENT_SOURCE_DIR}/content/parser
    ${CMAKE_CURRENT_SOURCE_DIR}/handbook
    ${CMAKE_CURRENT_SOURCE_DIR}/handbook/builder)
target_link_libraries(qc_common PUBLIC Qt::Core)
