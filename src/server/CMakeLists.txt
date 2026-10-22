qt_add_library(qc_server STATIC)
# Sources per folder, sorted, each header next to its sources (decision E-417;
# scripts/check-cmake-lists.py checks the lists).
target_sources(qc_server PRIVATE
    blockentities/QcBlockEntities.h blockentities/QcBlockEntities.cpp
    blockentities/QcBlockEntityChecks.h blockentities/QcBlockEntityChecks.cpp
    blockentities/QcBlockEntityExtras.h blockentities/QcBlockEntityExtras.cpp
    blockentities/QcServerSigns.h blockentities/QcServerSigns.cpp
    blockentities/QcSignTexts.h blockentities/QcSignTexts.cpp
)
target_sources(qc_server PRIVATE
    blocks/QcBlockDyeing.h blocks/QcBlockDyeing.cpp
    blocks/QcBlockRules.h blocks/QcBlockRules.cpp
    blocks/QcBlockSimulation.h blocks/QcBlockSimulation.cpp
    blocks/QcBlockUpdateQueue.h blocks/QcBlockUpdateQueue.cpp
    blocks/QcFallingBlockManager.h blocks/QcFallingBlockManager.cpp
    blocks/QcFireIgnition.h blocks/QcFireIgnition.cpp
    blocks/QcFireQueue.h blocks/QcFireQueue.cpp
    blocks/QcFireRules.h blocks/QcFireRules.cpp
    blocks/QcLiquidFlow.h blocks/QcLiquidFlow.cpp
    blocks/QcOpeners.h blocks/QcOpeners.cpp
    blocks/QcRails.h blocks/QcRails.cpp
    blocks/QcServerBlocks.h blocks/QcServerBlocks.cpp
    blocks/QcServerFallingBlocks.h blocks/QcServerFallingBlocks.cpp
    blocks/QcServerFire.h blocks/QcServerFire.cpp
    blocks/QcSlabRules.h blocks/QcSlabRules.cpp
    blocks/QcStairShapes.h blocks/QcStairShapes.cpp
)
target_sources(qc_server PRIVATE
    chunks/QcBlockIndex.h chunks/QcBlockIndex.cpp
    chunks/QcChunkGroups.h chunks/QcChunkGroups.cpp
    chunks/QcChunkLinks.h chunks/QcChunkLinks.cpp
    chunks/QcChunkLoaders.h chunks/QcChunkLoaders.cpp
    chunks/QcChunkLoading.h chunks/QcChunkLoading.cpp
    chunks/QcChunkSaveState.h chunks/QcChunkSaveState.cpp
    chunks/QcChunkStreaming.h chunks/QcChunkStreaming.cpp
    chunks/QcChunkTickets.h chunks/QcChunkTickets.cpp
    chunks/QcSectionMarks.h chunks/QcSectionMarks.cpp
    chunks/QcSignalChunks.h chunks/QcSignalChunks.cpp
)
target_sources(qc_server PRIVATE
    commands/QcAccessList.h commands/QcAccessList.cpp
    commands/QcAdventureCommands.h commands/QcAdventureCommands.cpp
    commands/QcBackupRunner.h commands/QcBackupRunner.cpp
    commands/QcBlockCommands.h commands/QcBlockCommands.cpp
    commands/QcCheatCommands.h commands/QcCheatCommands.cpp
    commands/QcChunkCommands.h commands/QcChunkCommands.cpp
    commands/QcChunkRegeneration.h commands/QcChunkRegeneration.cpp
    commands/QcCommandArguments.h commands/QcCommandArguments.cpp
    commands/QcCommandRegistry.h commands/QcCommandRegistry.cpp
    commands/QcCommandSource.h commands/QcCommandSource.cpp
    commands/QcEffectCommands.h commands/QcEffectCommands.cpp
    commands/QcExperienceCommands.h commands/QcExperienceCommands.cpp
    commands/QcMobCommands.h commands/QcMobCommands.cpp
    commands/QcPendingCommands.h commands/QcPendingCommands.cpp
    commands/QcPermission.h
    commands/QcPlayerCommands.h commands/QcPlayerCommands.cpp
    commands/QcServerCommands.h commands/QcServerCommands.cpp
    commands/QcStandCommands.h commands/QcStandCommands.cpp
    commands/QcTickCommands.h commands/QcTickCommands.cpp
    commands/QcVillageCommands.h commands/QcVillageCommands.cpp
    commands/QcWeatherCommands.h commands/QcWeatherCommands.cpp
    commands/QcWorldBackup.h commands/QcWorldBackup.cpp
    commands/QcWorldCommands.h commands/QcWorldCommands.cpp
)
target_sources(qc_server PRIVATE
    core/QcContactDamage.h core/QcContactDamage.cpp
    core/QcEntitySync.h core/QcEntitySync.cpp
    core/QcRateLimiter.h core/QcRateLimiter.cpp
    core/QcServer.h core/QcServer.cpp
    core/QcServerClient.h
    core/QcServerConfig.h
    core/QcServerDebug.h core/QcServerDebug.cpp
    core/QcServerLimits.h core/QcServerLimits.cpp
    core/QcServerLogging.h core/QcServerLogging.cpp
    core/QcServerSetup.h core/QcServerSetup.cpp
    core/QcServerSounds.h core/QcServerSounds.cpp
    core/QcServerState.h core/QcServerState.cpp
    core/QcServerSystems.h
    core/QcServerTick.cpp
    core/QcServerWorld.h core/QcServerWorld.cpp
    core/QcSpawnPoint.h core/QcSpawnPoint.cpp
)
target_sources(qc_server PRIVATE
    entities/QcArmourStand.h
    entities/QcComponent.h
    entities/QcCreature.h
    entities/QcEntities.h entities/QcEntities.cpp
    entities/QcEntity.h
    entities/QcEntityChecks.h entities/QcEntityChecks.cpp
    entities/QcEntityComponent.h
    entities/QcExperienceOrb.h
    entities/QcFallingBlock.h
    entities/QcHangingEntity.h
    entities/QcHurtWindow.h entities/QcHurtWindow.cpp
    entities/QcItemEntity.h
    entities/QcMinecart.h
    entities/QcMovingBlock.h
    entities/QcPrimedExplosive.h
    entities/QcProjectile.h
)
target_sources(qc_server PRIVATE
    explosions/QcBlastBlocks.h explosions/QcBlastBlocks.cpp
    explosions/QcBlastBodies.h explosions/QcBlastBodies.cpp
    explosions/QcBlastField.h explosions/QcBlastField.cpp
    explosions/QcBlastTable.h explosions/QcBlastTable.cpp
    explosions/QcExplosionRays.h explosions/QcExplosionRays.cpp
    explosions/QcPrimedExplosives.h explosions/QcPrimedExplosives.cpp
    explosions/QcServerExplosions.h explosions/QcServerExplosions.cpp
)
target_sources(qc_server PRIVATE
    gameplay/QcArmourStandManager.h gameplay/QcArmourStandManager.cpp
    gameplay/QcBodyBurning.h gameplay/QcBodyBurning.cpp
    gameplay/QcEnvironmentDamage.h gameplay/QcEnvironmentDamage.cpp
    gameplay/QcHunger.h gameplay/QcHunger.cpp
    gameplay/QcLineOfSight.h gameplay/QcLineOfSight.cpp
    gameplay/QcPlayerHealth.h gameplay/QcPlayerHealth.cpp
    gameplay/QcSafeSpot.h gameplay/QcSafeSpot.cpp
    gameplay/QcServerCombat.h gameplay/QcServerCombat.cpp
    gameplay/QcServerCrafting.h gameplay/QcServerCrafting.cpp
    gameplay/QcServerEffects.h gameplay/QcServerEffects.cpp
    gameplay/QcServerGameModes.h gameplay/QcServerGameModes.cpp
    gameplay/QcServerInteraction.h gameplay/QcServerInteraction.cpp
    gameplay/QcServerInventory.h gameplay/QcServerInventory.cpp
    gameplay/QcServerStands.h gameplay/QcServerStands.cpp
    gameplay/QcSpectatorRules.h gameplay/QcSpectatorRules.cpp
    gameplay/QcStandProperties.h gameplay/QcStandProperties.cpp
)
target_sources(qc_server PRIVATE
    golems/QcGolemBrain.h golems/QcGolemBrain.cpp
    golems/QcGolemGrudges.h golems/QcGolemGrudges.cpp
    golems/QcGolemState.h
    golems/QcServerGolems.h golems/QcServerGolems.cpp
)
target_sources(qc_server PRIVATE
    guardians/QcCrumbling.h guardians/QcCrumbling.cpp
    guardians/QcGuardianBrain.h guardians/QcGuardianBrain.cpp
    guardians/QcGuardians.h guardians/QcGuardians.cpp
    guardians/QcGuardPost.h
)
target_sources(qc_server PRIVATE
    hanging/QcHangingManager.h hanging/QcHangingManager.cpp
    hanging/QcHangingRules.h hanging/QcHangingRules.cpp
    hanging/QcServerHanging.h hanging/QcServerHanging.cpp
)
target_sources(qc_server PRIVATE
    items/QcExperienceOrbManager.h items/QcExperienceOrbManager.cpp
    items/QcItemEntityManager.h items/QcItemEntityManager.cpp
    items/QcServerExperience.h items/QcServerExperience.cpp
    items/QcServerItems.h items/QcServerItems.cpp
)
target_sources(qc_server PRIVATE
    login/QcIdentityStore.h login/QcIdentityStore.cpp
    login/QcPlayerJoin.h login/QcPlayerJoin.cpp
    login/QcServerLogin.h login/QcServerLogin.cpp
)
target_sources(qc_server PRIVATE
    machines/QcBlockScreens.h machines/QcBlockScreens.cpp
    machines/QcCampfires.h machines/QcCampfires.cpp
    machines/QcInventoryAccess.h machines/QcInventoryAccess.cpp
    machines/QcMachineRules.h machines/QcMachineRules.cpp
    machines/QcServerKitchen.h machines/QcServerKitchen.cpp
    machines/QcServerMachines.h machines/QcServerMachines.cpp
    machines/QcServerStonecutter.h machines/QcServerStonecutter.cpp
)
target_sources(qc_server PRIVATE
    maps/QcMapPending.h maps/QcMapPending.cpp
    maps/QcMapSaveCodec.h maps/QcMapSaveCodec.cpp
    maps/QcMapStore.h maps/QcMapStore.cpp
    maps/QcMapSurvey.h maps/QcMapSurvey.cpp
    maps/QcServerMaps.h maps/QcServerMaps.cpp
)
target_sources(qc_server PRIVATE
    minecarts/QcMinecartCollisions.h minecarts/QcMinecartCollisions.cpp
    minecarts/QcMinecartHoppers.h minecarts/QcMinecartHoppers.cpp
    minecarts/QcMinecartManager.h minecarts/QcMinecartManager.cpp
    minecarts/QcMinecartMotion.h minecarts/QcMinecartMotion.cpp
    minecarts/QcServerMinecarts.h minecarts/QcServerMinecarts.cpp
)
target_sources(qc_server PRIVATE
    mobs/QcArcherBrain.h mobs/QcArcherBrain.cpp
    mobs/QcArcherState.h
    mobs/QcCreatureBrains.h mobs/QcCreatureBrains.cpp
    mobs/QcCreatureTraits.h mobs/QcCreatureTraits.cpp
    mobs/QcFishSwimming.h mobs/QcFishSwimming.cpp
    mobs/QcInfestation.h mobs/QcInfestation.cpp
    mobs/QcMobBehaviour.h mobs/QcMobBehaviour.cpp
    mobs/QcMobBrain.h
    mobs/QcMobBreeding.h mobs/QcMobBreeding.cpp
    mobs/QcMobCramming.h mobs/QcMobCramming.cpp
    mobs/QcMobCrowd.h mobs/QcMobCrowd.cpp
    mobs/QcMobDrinking.h mobs/QcMobDrinking.cpp
    mobs/QcMobExchange.h
    mobs/QcMobFear.h mobs/QcMobFear.cpp
    mobs/QcMobLaying.h mobs/QcMobLaying.cpp
    mobs/QcMobManager.h mobs/QcMobManager.cpp
    mobs/QcMobSteering.h mobs/QcMobSteering.cpp
    mobs/QcMobTeleport.h mobs/QcMobTeleport.cpp
    mobs/QcMobUpkeep.h mobs/QcMobUpkeep.cpp
    mobs/QcNeutralBrain.h mobs/QcNeutralBrain.cpp
    mobs/QcPetCare.h mobs/QcPetCare.cpp
    mobs/QcPetOwners.h mobs/QcPetOwners.cpp
    mobs/QcServerAnimals.h mobs/QcServerAnimals.cpp
    mobs/QcServerCreatures.h mobs/QcServerCreatures.cpp
    mobs/QcTameBrain.h mobs/QcTameBrain.cpp
    mobs/QcTameState.h
)
target_sources(qc_server PRIVATE
    network/QcConnectionLimiter.h network/QcConnectionLimiter.cpp
    network/QcNetworkServer.h network/QcNetworkServer.cpp
    network/QcServerSettings.h network/QcServerSettings.cpp
)
target_sources(qc_server PRIVATE
    pathing/QcDoorPassage.h pathing/QcDoorPassage.cpp
    pathing/QcMobLegs.h pathing/QcMobLegs.cpp
    pathing/QcMobNavigator.h pathing/QcMobNavigator.cpp
    pathing/QcMobPath.h
    pathing/QcPathCells.h pathing/QcPathCells.cpp
    pathing/QcPathFinder.h pathing/QcPathFinder.cpp
    pathing/QcPathGround.h
    pathing/QcPathPlanner.h pathing/QcPathPlanner.cpp
    pathing/QcPathTerrain.h pathing/QcPathTerrain.cpp
    pathing/QcPathWindow.h pathing/QcPathWindow.cpp
)
target_sources(qc_server PRIVATE
    plants/QcCropGrowth.h plants/QcCropGrowth.cpp
    plants/QcPlantGrowth.h plants/QcPlantGrowth.cpp
    plants/QcPlantManager.h plants/QcPlantManager.cpp
    plants/QcServerPlants.h plants/QcServerPlants.cpp
    plants/QcVegetationSpread.h plants/QcVegetationSpread.cpp
)
target_sources(qc_server PRIVATE
    projectiles/QcHitEffects.h projectiles/QcHitEffects.cpp
    projectiles/QcPlayerFishing.h projectiles/QcPlayerFishing.cpp
    projectiles/QcPlayerLaunchers.h projectiles/QcPlayerLaunchers.cpp
    projectiles/QcPlayerThrows.h projectiles/QcPlayerThrows.cpp
    projectiles/QcProjectileFlight.h projectiles/QcProjectileFlight.cpp
    projectiles/QcProjectileManager.h projectiles/QcProjectileManager.cpp
    projectiles/QcServerProjectiles.h projectiles/QcServerProjectiles.cpp
)
target_sources(qc_server PRIVATE
    signal/QcCircuitComponents.h signal/QcCircuitComponents.cpp
    signal/QcCircuitPlan.h
    signal/QcCircuitState.h signal/QcCircuitState.cpp
    signal/QcComparatorReadings.h signal/QcComparatorReadings.cpp
    signal/QcDispenserActions.h signal/QcDispenserActions.cpp
    signal/QcDustNetwork.h signal/QcDustNetwork.cpp
    signal/QcHopperQueue.h signal/QcHopperQueue.cpp
    signal/QcMovingBlocks.h signal/QcMovingBlocks.cpp
    signal/QcNoteBudget.h signal/QcNoteBudget.cpp
    signal/QcPistonMover.h signal/QcPistonMover.cpp
    signal/QcPistonPlanner.h signal/QcPistonPlanner.cpp
    signal/QcServerCircuits.h signal/QcServerCircuits.cpp
    signal/QcServerHoppers.h signal/QcServerHoppers.cpp
    signal/QcSignalPower.h signal/QcSignalPower.cpp
    signal/QcSignalSchedule.h signal/QcSignalSchedule.cpp
    signal/QcSignalTable.h signal/QcSignalTable.cpp
    signal/QcTripwires.h signal/QcTripwires.cpp
)
target_sources(qc_server PRIVATE
    spawners/QcFurnishing.h spawners/QcFurnishing.cpp
    spawners/QcSpawnerRules.h spawners/QcSpawnerRules.cpp
    spawners/QcSpawners.h spawners/QcSpawners.cpp
)
target_sources(qc_server PRIVATE
    spawning/QcMonsterCap.h spawning/QcMonsterCap.cpp
    spawning/QcNaturalSpawner.h spawning/QcNaturalSpawner.cpp
    spawning/QcSpawnLight.h spawning/QcSpawnLight.cpp
)
target_sources(qc_server PRIVATE
    storage/QcBlockEntityCodec.h storage/QcBlockEntityCodec.cpp
    storage/QcBlockEntityRecord.h
    storage/QcBlockEntityRecords.h storage/QcBlockEntityRecords.cpp
    storage/QcChunkCodec.h storage/QcChunkCodec.cpp
    storage/QcChunkIo.h storage/QcChunkIo.cpp
    storage/QcChunkStore.h storage/QcChunkStore.cpp
    storage/QcComponentFrames.h storage/QcComponentFrames.cpp
    storage/QcCreatureComponents.h storage/QcCreatureComponents.cpp
    storage/QcEntityCodec.h storage/QcEntityCodec.cpp
    storage/QcEntityRecord.h
    storage/QcEntityRecords.h storage/QcEntityRecords.cpp
    storage/QcHangingCodec.h storage/QcHangingCodec.cpp
    storage/QcItemDataCodec.h storage/QcItemDataCodec.cpp
    storage/QcLoadWarnings.h
    storage/QcPlayerRecords.h storage/QcPlayerRecords.cpp
    storage/QcPlayerStorage.h storage/QcPlayerStorage.cpp
    storage/QcRegionFile.h storage/QcRegionFile.cpp
    storage/QcSaveFile.h storage/QcSaveFile.cpp
    storage/QcServerPersistence.h storage/QcServerPersistence.cpp
    storage/QcStackReader.h storage/QcStackReader.cpp
    storage/QcVillagerCodec.h storage/QcVillagerCodec.cpp
    storage/QcWorldStorage.h storage/QcWorldStorage.cpp
)
target_sources(qc_server PRIVATE
    tick/QcClientSyncPace.h tick/QcClientSyncPace.cpp
    tick/QcLagReport.h tick/QcLagReport.cpp
    tick/QcServerClock.h
    tick/QcTickControl.h
    tick/QcTickPhaseTimer.h tick/QcTickPhaseTimer.cpp
    tick/QcTickScheduler.h tick/QcTickScheduler.cpp
    tick/QcTickStats.h tick/QcTickStats.cpp
    tick/QcTickSystems.h tick/QcTickSystems.cpp
)
target_sources(qc_server PRIVATE
    villages/QcServerTrading.h villages/QcServerTrading.cpp
    villages/QcServerVillagers.h villages/QcServerVillagers.cpp
    villages/QcVillage.h villages/QcVillage.cpp
    villages/QcVillageGolems.h villages/QcVillageGolems.cpp
    villages/QcVillagePopulation.h villages/QcVillagePopulation.cpp
    villages/QcVillager.h
    villages/QcVillagerBehaviour.h villages/QcVillagerBehaviour.cpp
    villages/QcVillagerBreeding.h villages/QcVillagerBreeding.cpp
    villages/QcVillagerHomes.h villages/QcVillagerHomes.cpp
    villages/QcVillagerLooks.h villages/QcVillagerLooks.cpp
    villages/QcVillagerRelocation.h villages/QcVillagerRelocation.cpp
    villages/QcVillagerRoads.h villages/QcVillagerRoads.cpp
    villages/QcVillagerTrades.h villages/QcVillagerTrades.cpp
    villages/QcVillages.h villages/QcVillages.cpp
)
target_sources(qc_server PRIVATE
    weather/QcLightningStrike.h weather/QcLightningStrike.cpp
    weather/QcServerWeather.h weather/QcServerWeather.cpp
    weather/QcSnowfall.h weather/QcSnowfall.cpp
    weather/QcWeatherCycle.h weather/QcWeatherCycle.cpp
)
qc_configure_target(qc_server)
# Includes name the header by its path below src/ ("server/blocks/QcRails.h", decision E-418):
# src/ is the only include directory a library passes on.
target_include_directories(qc_server PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(qc_server PUBLIC qc_base_content qc_common qc_worldgen qc_network Qt::Core Qt::Concurrent)
