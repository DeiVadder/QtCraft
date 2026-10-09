qt_add_library(qc_server STATIC
    QcBaseContent.h QcBaseContent.cpp
    QcBlockEntityStorage.h QcBlockEntityStorage.cpp
    QcBlockRules.h QcBlockRules.cpp
    QcBlockSimulation.h QcBlockSimulation.cpp
    QcBlockUpdateQueue.h QcBlockUpdateQueue.cpp
    QcBundledMods.h QcBundledMods.cpp
    QcChatCommands.h QcChatCommands.cpp
    QcChunkStreaming.h QcChunkStreaming.cpp
    QcContainerManager.h QcContainerManager.cpp
    QcContentPackLoader.h QcContentPackLoader.cpp
    QcEntityStorage.h QcEntityStorage.cpp
    QcEntitySync.h QcEntitySync.cpp
    QcHunger.h QcHunger.cpp
    QcHandbookRules.h QcHandbookRules.cpp
    QcItemEntityManager.h QcItemEntityManager.cpp
    QcFallingBlockManager.h QcFallingBlockManager.cpp
    QcLiquidFlow.h QcLiquidFlow.cpp
    QcLocalServerHost.h QcLocalServerHost.cpp
    QcMachineManager.h QcMachineManager.cpp
    QcModLoader.h QcModLoader.cpp
    QcModStatus.h
    QcMobManager.h QcMobManager.cpp
    QcPlantManager.h QcPlantManager.cpp
    QcRegionStorage.h QcRegionStorage.cpp
    QcSaveFile.h QcSaveFile.cpp
    QcServer.h QcServer.cpp
    QcServerBlocks.h QcServerBlocks.cpp
    QcServerClient.h
    QcServerCombat.h QcServerCombat.cpp
    QcServerConfig.h
    QcServerInteraction.h QcServerInteraction.cpp
    QcServerInventory.h QcServerInventory.cpp
    QcServerItemsAndPlants.h QcServerItemsAndPlants.cpp
    QcServerMachines.h QcServerMachines.cpp
    QcServerPersistence.h QcServerPersistence.cpp
    QcServerSounds.h QcServerSounds.cpp
    QcServerState.h QcServerState.cpp
    QcServerWorld.h QcServerWorld.cpp
    QcTestTerrainGenerator.h QcTestTerrainGenerator.cpp
    QcWorldGenerator.h
    QcWorldLibrary.h QcWorldLibrary.cpp
    QcWorldStorage.h QcWorldStorage.cpp
    worldgen/QcCaveCarver.h worldgen/QcCaveCarver.cpp
    worldgen/QcNoise.h worldgen/QcNoise.cpp
    worldgen/QcPoolPlacer.h worldgen/QcPoolPlacer.cpp
    worldgen/QcTerrainColumn.h
    worldgen/QcTerrainColumnGrid.h worldgen/QcTerrainColumnGrid.cpp
    worldgen/QcTerrainDefinitions.h worldgen/QcTerrainDefinitions.cpp
    worldgen/QcTerrainGenerator.h worldgen/QcTerrainGenerator.cpp
    worldgen/QcTerrainMath.h
    worldgen/QcTreeShape.h worldgen/QcTreeShape.cpp
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
target_include_directories(qc_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/worldgen)
target_link_libraries(qc_server PUBLIC qc_common qc_base_content Qt::Core Qt::Concurrent)
