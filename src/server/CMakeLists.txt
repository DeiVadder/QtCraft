qt_add_library(qc_server STATIC
    network/QcServerSettings.h network/QcServerSettings.cpp
    content/QcContentFingerprint.h content/QcContentFingerprint.cpp
    network/QcConnectionLimiter.h network/QcConnectionLimiter.cpp
    network/QcNetworkServer.h network/QcNetworkServer.cpp
    login/QcIdentityStore.h login/QcIdentityStore.cpp
    login/QcServerLogin.h login/QcServerLogin.cpp
    login/QcPlayerJoin.h login/QcPlayerJoin.cpp
    commands/QcAccessList.h commands/QcAccessList.cpp
    commands/QcBlockCommands.h commands/QcBlockCommands.cpp
    commands/QcCheatCommands.h commands/QcCheatCommands.cpp
    commands/QcChunkCommands.h commands/QcChunkCommands.cpp
    commands/QcChunkRegeneration.h commands/QcChunkRegeneration.cpp
    commands/QcCommandArguments.h commands/QcCommandArguments.cpp
    commands/QcCommandRegistry.h commands/QcCommandRegistry.cpp
    commands/QcCommandSource.h commands/QcCommandSource.cpp
    commands/QcPendingCommands.h commands/QcPendingCommands.cpp
    commands/QcPermission.h
    commands/QcPlayerCommands.h commands/QcPlayerCommands.cpp
    commands/QcServerCommands.h commands/QcServerCommands.cpp
    commands/QcStandCommands.h commands/QcStandCommands.cpp
    commands/QcTickCommands.h commands/QcTickCommands.cpp
    commands/QcWorldBackup.h commands/QcWorldBackup.cpp
    commands/QcBackupRunner.h commands/QcBackupRunner.cpp
    commands/QcWorldCommands.h commands/QcWorldCommands.cpp
    content/QcBaseContent.h content/QcBaseContent.cpp
    storage/QcBlockEntityRecord.h
    storage/QcBlockEntityRecords.h storage/QcBlockEntityRecords.cpp
    storage/QcBlockEntityCodec.h storage/QcBlockEntityCodec.cpp
    storage/QcChunkCodec.h storage/QcChunkCodec.cpp
    storage/QcChunkIo.h storage/QcChunkIo.cpp
    storage/QcChunkStore.h storage/QcChunkStore.cpp
    storage/QcRegionFile.h storage/QcRegionFile.cpp
    storage/QcStackReader.h storage/QcStackReader.cpp
    blocks/QcBlockRules.h blocks/QcBlockRules.cpp
    blocks/QcBlockSimulation.h blocks/QcBlockSimulation.cpp
    blocks/QcServerFallingBlocks.h blocks/QcServerFallingBlocks.cpp
    blocks/QcBlockUpdateQueue.h blocks/QcBlockUpdateQueue.cpp
    blocks/QcOpeners.h blocks/QcOpeners.cpp
    content/QcBundledMods.h content/QcBundledMods.cpp
    core/QcChunkGroups.h core/QcChunkGroups.cpp
    core/QcChunkLinks.h core/QcChunkLinks.cpp
    core/QcChunkLoaders.h core/QcChunkLoaders.cpp
    core/QcChunkLoading.h core/QcChunkLoading.cpp
    core/QcChunkStreaming.h core/QcChunkStreaming.cpp
    core/QcChunkTickets.h core/QcChunkTickets.cpp
    blockentities/QcBlockEntities.h blockentities/QcBlockEntities.cpp
    entities/QcEntity.h
    entities/QcEntities.h entities/QcEntities.cpp
    entities/QcEntityChecks.h entities/QcEntityChecks.cpp
    entities/QcCreature.h
    entities/QcItemEntity.h
    entities/QcFallingBlock.h
    entities/QcArmourStand.h
    entities/QcMovingBlock.h
    blockentities/QcBlockEntityChecks.h blockentities/QcBlockEntityChecks.cpp
    content/QcContentPackLoader.h content/QcContentPackLoader.cpp
    storage/QcEntityRecord.h
    storage/QcEntityRecords.h storage/QcEntityRecords.cpp
    storage/QcEntityCodec.h storage/QcEntityCodec.cpp
    core/QcEntitySync.h core/QcEntitySync.cpp
    gameplay/QcHunger.h gameplay/QcHunger.cpp
    gameplay/QcCropGrowth.h gameplay/QcCropGrowth.cpp
    gameplay/QcPlantGrowth.h gameplay/QcPlantGrowth.cpp
    gameplay/QcHandbookRules.h gameplay/QcHandbookRules.cpp
    gameplay/QcArmourStandManager.h gameplay/QcArmourStandManager.cpp
    gameplay/QcItemEntityManager.h gameplay/QcItemEntityManager.cpp
    gameplay/QcServerStands.h gameplay/QcServerStands.cpp
    gameplay/QcStandProperties.h gameplay/QcStandProperties.cpp
    gameplay/QcLineOfSight.h gameplay/QcLineOfSight.cpp
    blocks/QcFallingBlockManager.h blocks/QcFallingBlockManager.cpp
    blocks/QcLiquidFlow.h blocks/QcLiquidFlow.cpp
    core/QcLocalServerHost.h core/QcLocalServerHost.cpp
    machines/QcMachineRules.h machines/QcMachineRules.cpp
    content/QcModLoader.h content/QcModLoader.cpp
    content/QcModStatus.h
    gameplay/QcMobBreeding.h gameplay/QcMobBreeding.cpp
    gameplay/QcMobManager.h gameplay/QcMobManager.cpp
    gameplay/QcMobSteering.h gameplay/QcMobSteering.cpp
    gameplay/QcMobUpkeep.h gameplay/QcMobUpkeep.cpp
    gameplay/QcPlantManager.h gameplay/QcPlantManager.cpp
    storage/QcSaveFile.h storage/QcSaveFile.cpp
    storage/QcPlayerStorage.h storage/QcPlayerStorage.cpp
    storage/QcPlayerRecords.h storage/QcPlayerRecords.cpp
    core/QcProfileValidation.h core/QcProfileValidation.cpp
    core/QcRateLimiter.h core/QcRateLimiter.cpp
    core/QcServer.h core/QcServer.cpp
    blocks/QcServerBlocks.h blocks/QcServerBlocks.cpp
    core/QcServerClient.h
    gameplay/QcServerCombat.h gameplay/QcServerCombat.cpp
    gameplay/QcPlayerHealth.h gameplay/QcPlayerHealth.cpp
    gameplay/QcEnvironmentDamage.h gameplay/QcEnvironmentDamage.cpp
    gameplay/QcServerCreatures.h gameplay/QcServerCreatures.cpp
    gameplay/QcServerAnimals.h gameplay/QcServerAnimals.cpp
    gameplay/QcServerGameModes.h gameplay/QcServerGameModes.cpp
    core/QcServerConfig.h
    gameplay/QcServerInteraction.h gameplay/QcServerInteraction.cpp
    gameplay/QcServerInventory.h gameplay/QcServerInventory.cpp
    gameplay/QcServerCrafting.h gameplay/QcServerCrafting.cpp
    gameplay/QcServerItems.h gameplay/QcServerItems.cpp
    gameplay/QcServerPlants.h gameplay/QcServerPlants.cpp
    machines/QcBlockScreens.h machines/QcBlockScreens.cpp
    machines/QcServerMachines.h machines/QcServerMachines.cpp
    machines/QcInventoryAccess.h machines/QcInventoryAccess.cpp
    storage/QcServerPersistence.h storage/QcServerPersistence.cpp
    core/QcSectionMarks.h core/QcSectionMarks.cpp
    core/QcBlockIndex.h core/QcBlockIndex.cpp
    core/QcServerLimits.h core/QcServerLimits.cpp
    core/QcServerSetup.h core/QcServerSetup.cpp
    core/QcServerSounds.h core/QcServerSounds.cpp
    core/QcContactDamage.h core/QcContactDamage.cpp
    core/QcServerState.h core/QcServerState.cpp
    core/QcSpawnPoint.h core/QcSpawnPoint.cpp
    core/QcServerWorld.h core/QcServerWorld.cpp
    core/QcServerDebug.h core/QcServerDebug.cpp
    core/QcTickPhaseTimer.h core/QcTickPhaseTimer.cpp
    core/QcTickControl.h
    core/QcTickScheduler.h core/QcTickScheduler.cpp
    core/QcTickStats.h core/QcTickStats.cpp
    worldgen/QcTestTerrainGenerator.h worldgen/QcTestTerrainGenerator.cpp
    worldgen/QcWorldGenerator.h
    storage/QcWorldLibrary.h storage/QcWorldLibrary.cpp
    storage/QcWorldStorage.h storage/QcWorldStorage.cpp
    worldgen/QcCaveCarver.h worldgen/QcCaveCarver.cpp
    worldgen/QcNoise.h worldgen/QcNoise.cpp
    worldgen/QcPoolPlacer.h worldgen/QcPoolPlacer.cpp
    worldgen/QcTerrainColumn.h
    worldgen/QcTerrainColumnGrid.h worldgen/QcTerrainColumnGrid.cpp
    worldgen/QcTerrainDefinitions.h worldgen/QcTerrainDefinitions.cpp
    worldgen/QcTerrainGenerator.h worldgen/QcTerrainGenerator.cpp
    worldgen/QcTerrainMath.h
    worldgen/QcTreeShape.h worldgen/QcTreeShape.cpp
    worldgen/QcStateRotation.h worldgen/QcStateRotation.cpp
    worldgen/QcStructure.h worldgen/QcStructure.cpp
    worldgen/QcStructurePlacement.h worldgen/QcStructurePlacement.cpp
    worldgen/QcWorldGenerators.h worldgen/QcWorldGenerators.cpp
    worldgen/QcFlatGenerator.h worldgen/QcFlatGenerator.cpp
    worldgen/QcVoidGenerator.h worldgen/QcVoidGenerator.cpp
    worldgen/QcWorldSetup.h
    worldgen/showcase/QcShowcaseArea.h
    worldgen/showcase/QcShowcaseArmourArea.h worldgen/showcase/QcShowcaseArmourArea.cpp
    worldgen/showcase/QcShowcaseBlockArea.h worldgen/showcase/QcShowcaseBlockArea.cpp
    worldgen/showcase/QcShowcaseCategories.h worldgen/showcase/QcShowcaseCategories.cpp
    worldgen/showcase/QcShowcaseCreatureArea.h worldgen/showcase/QcShowcaseCreatureArea.cpp
    worldgen/showcase/QcShowcaseGenerator.h worldgen/showcase/QcShowcaseGenerator.cpp
    worldgen/showcase/QcShowcaseItemArea.h worldgen/showcase/QcShowcaseItemArea.cpp
    worldgen/showcase/QcShowcasePlan.h
    worldgen/showcase/QcShowcasePlanner.h worldgen/showcase/QcShowcasePlanner.cpp
    worldgen/showcase/QcShowcaseTreeArea.h worldgen/showcase/QcShowcaseTreeArea.cpp
    signal/QcCircuitComponents.h signal/QcCircuitComponents.cpp
    signal/QcComparatorReadings.h signal/QcComparatorReadings.cpp
    signal/QcDispenserActions.h signal/QcDispenserActions.cpp
    signal/QcHopperQueue.h signal/QcHopperQueue.cpp
    signal/QcServerHoppers.h signal/QcServerHoppers.cpp
    signal/QcCircuitPlan.h
    signal/QcCircuitState.h signal/QcCircuitState.cpp
    signal/QcDustNetwork.h signal/QcDustNetwork.cpp
    signal/QcMovingBlocks.h signal/QcMovingBlocks.cpp
    signal/QcPistonMover.h signal/QcPistonMover.cpp
    signal/QcPistonPlanner.h signal/QcPistonPlanner.cpp
    signal/QcServerCircuits.h signal/QcServerCircuits.cpp
    signal/QcSignalPower.h signal/QcSignalPower.cpp
    signal/QcSignalSchedule.h signal/QcSignalSchedule.cpp
    signal/QcSignalTable.h signal/QcSignalTable.cpp
)
qc_configure_target(qc_server)
# Identical terrain on every compiler and CPU: no fused multiply-add contraction in noise maths.
set(qc_worldgen_sources
    worldgen/QcCaveCarver.cpp worldgen/QcNoise.cpp worldgen/QcPoolPlacer.cpp worldgen/QcTerrainColumnGrid.cpp
    worldgen/QcTerrainDefinitions.cpp worldgen/QcTerrainGenerator.cpp worldgen/QcTreeShape.cpp)
if(MSVC)
    set_source_files_properties(${qc_worldgen_sources} PROPERTIES COMPILE_OPTIONS /fp:precise)
else()
    set_source_files_properties(${qc_worldgen_sources} PROPERTIES COMPILE_OPTIONS -ffp-contract=off)
endif()
# Classes live in thematic subfolders (E-089); every folder is an include directory, so includes
# stay "QcName.h" without a path.
target_include_directories(qc_server PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/commands
    ${CMAKE_CURRENT_SOURCE_DIR}/login
    ${CMAKE_CURRENT_SOURCE_DIR}/network
    ${CMAKE_CURRENT_SOURCE_DIR}/blocks
    ${CMAKE_CURRENT_SOURCE_DIR}/gameplay
    ${CMAKE_CURRENT_SOURCE_DIR}/machines
    ${CMAKE_CURRENT_SOURCE_DIR}/blockentities
    ${CMAKE_CURRENT_SOURCE_DIR}/entities
    ${CMAKE_CURRENT_SOURCE_DIR}/signal
    ${CMAKE_CURRENT_SOURCE_DIR}/storage
    ${CMAKE_CURRENT_SOURCE_DIR}/content
    ${CMAKE_CURRENT_SOURCE_DIR}/worldgen
    ${CMAKE_CURRENT_SOURCE_DIR}/worldgen/showcase)
target_link_libraries(qc_server PUBLIC qc_common qc_network qc_base_content Qt::Core Qt::Concurrent)
