qt_add_library(qc_common STATIC
    QcAabb.h
    QcBlockAccess.h
    QcBlockDefinition.h
    QcBlockProperty.h
    QcBlockState.h
    QcBlockStateTable.h QcBlockStateTable.cpp
    QcShapeCondition.h
    QcShapeDefinition.h
    QcShapePart.h
    QcLightVolume.h QcLightVolume.cpp
    QcBlockFace.h
    QcBlockPos.h QcBlockPos.cpp
    QcChunk.h QcChunk.cpp
    QcChunkPos.h QcChunkPos.cpp
    QcChunkSection.h QcChunkSection.cpp
    QcConnection.h QcConnection.cpp
    QcContentNames.h QcContentNames.cpp
    QcCrc32.h QcCrc32.cpp
    QcDayCycle.h QcDayCycle.cpp
    QcBlockParser.h QcBlockParser.cpp
    QcContentFields.h QcContentFields.cpp
    QcContentParser.h QcContentParser.cpp
    QcItemParser.h QcItemParser.cpp
    QcJsonFieldReader.h QcJsonFieldReader.cpp
    QcMaterialParser.h QcMaterialParser.cpp
    QcMobParser.h QcMobParser.cpp
    QcRecipeParser.h QcRecipeParser.cpp
    QcShapeParser.h QcShapeParser.cpp
    QcSoundParser.h QcSoundParser.cpp
    QcWorldgenParser.h QcWorldgenParser.cpp
    QcContentRegistry.h QcContentRegistry.cpp
    QcBlockSerializer.h QcBlockSerializer.cpp
    QcContentSerializer.h QcContentSerializer.cpp
    QcPackReference.h
    QcPartFamily.h
    QcPartGenerator.h QcPartGenerator.cpp
    QcMaterialDefinition.h
    QcCrafting.h QcCrafting.cpp
    QcEntityMotion.h
    QcHandbook.h QcHandbook.cpp
    QcHandbookBlockPages.h QcHandbookBlockPages.cpp
    QcHandbookBuilder.h QcHandbookBuilder.cpp
    QcHandbookChapter.h
    QcHandbookChapterPages.h QcHandbookChapterPages.cpp
    QcHandbookContext.h
    QcHandbookCreaturePages.h QcHandbookCreaturePages.cpp
    QcHandbookEntry.h
    QcHandbookIndex.h QcHandbookIndex.cpp
    QcHandbookItemPages.h QcHandbookItemPages.cpp
    QcHandbookJson.h QcHandbookJson.cpp
    QcHandbookLinks.h QcHandbookLinks.cpp
    QcHandbookMarkdown.h QcHandbookMarkdown.cpp
    QcHandbookOverviewPages.h QcHandbookOverviewPages.cpp
    QcHandbookRecipes.h QcHandbookRecipes.cpp
    QcHandbookText.h QcHandbookText.cpp
    QcHandbookWorldPages.h QcHandbookWorldPages.cpp
    QcInProcessTransport.h QcInProcessTransport.cpp
    QcIntMath.h
    QcInventory.h QcInventory.cpp
    QcItemDefinition.h
    QcItemStack.h
    QcKeyBindings.h QcKeyBindings.cpp
    QcLiquidDefinition.h
    QcLiquidReaction.h
    QcMachineDefinition.h
    QcMailbox.h
    QcMessages.h
    QcEntityMessages.h
    QcMachineMessages.h
    QcPlayerMessages.h
    QcSessionMessages.h
    QcWorldMessages.h
    QcMining.h QcMining.cpp
    QcAppearance.h
    QcAppearanceDefinition.h
    QcMobDefinition.h
    QcModelDefinition.h
    QcPhysics.h QcPhysics.cpp
    QcPlayerInput.h
    QcProtocol.h QcProtocol.cpp
    QcRaycast.h QcRaycast.cpp
    QcRecipeDefinition.h QcRecipeDefinition.cpp
    QcResult.h
    QcSmeltingRecipe.h
    QcSoundDefinition.h QcSoundDefinition.cpp
    QcSoundKeys.h QcSoundKeys.cpp
    QcStreamLimits.h
    QcTextureDefinition.h
    QcTransport.h
    QcVec3.h
    QcWorldgenDefinitions.h
    QcWorkerPool.h QcWorkerPool.cpp
    QcWorldConstants.h
)
qc_configure_target(qc_common)
target_include_directories(qc_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qc_common PUBLIC Qt::Core)
