qt_add_library(qc_server STATIC
    content/QcBaseContent.h content/QcBaseContent.cpp
    storage/QcBlockEntityStorage.h storage/QcBlockEntityStorage.cpp
    blocks/QcBlockRules.h blocks/QcBlockRules.cpp
    blocks/QcBlockSimulation.h blocks/QcBlockSimulation.cpp
    blocks/QcBlockUpdateQueue.h blocks/QcBlockUpdateQueue.cpp
    content/QcBundledMods.h content/QcBundledMods.cpp
    core/QcChatCommands.h core/QcChatCommands.cpp
    core/QcChunkStreaming.h core/QcChunkStreaming.cpp
    machines/QcContainerManager.h machines/QcContainerManager.cpp
    content/QcContentPackLoader.h content/QcContentPackLoader.cpp
    storage/QcEntityStorage.h storage/QcEntityStorage.cpp
    core/QcEntitySync.h core/QcEntitySync.cpp
    gameplay/QcHunger.h gameplay/QcHunger.cpp
    gameplay/QcHandbookRules.h gameplay/QcHandbookRules.cpp
    gameplay/QcItemEntityManager.h gameplay/QcItemEntityManager.cpp
    blocks/QcFallingBlockManager.h blocks/QcFallingBlockManager.cpp
    blocks/QcLiquidFlow.h blocks/QcLiquidFlow.cpp
    core/QcLocalServerHost.h core/QcLocalServerHost.cpp
    machines/QcMachineManager.h machines/QcMachineManager.cpp
    content/QcModLoader.h content/QcModLoader.cpp
    content/QcModStatus.h
    gameplay/QcMobManager.h gameplay/QcMobManager.cpp
    gameplay/QcPlantManager.h gameplay/QcPlantManager.cpp
    storage/QcRegionStorage.h storage/QcRegionStorage.cpp
    storage/QcSaveFile.h storage/QcSaveFile.cpp
    core/QcServer.h core/QcServer.cpp
    blocks/QcServerBlocks.h blocks/QcServerBlocks.cpp
    core/QcServerClient.h
    gameplay/QcServerCombat.h gameplay/QcServerCombat.cpp
    core/QcServerConfig.h
    gameplay/QcServerInteraction.h gameplay/QcServerInteraction.cpp
    gameplay/QcServerInventory.h gameplay/QcServerInventory.cpp
    gameplay/QcServerItemsAndPlants.h gameplay/QcServerItemsAndPlants.cpp
    machines/QcServerMachines.h machines/QcServerMachines.cpp
    storage/QcServerPersistence.h storage/QcServerPersistence.cpp
    core/QcServerSounds.h core/QcServerSounds.cpp
    core/QcServerState.h core/QcServerState.cpp
    core/QcServerWorld.h core/QcServerWorld.cpp
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
    ${CMAKE_CURRENT_SOURCE_DIR}/blocks
    ${CMAKE_CURRENT_SOURCE_DIR}/gameplay
    ${CMAKE_CURRENT_SOURCE_DIR}/machines
    ${CMAKE_CURRENT_SOURCE_DIR}/storage
    ${CMAKE_CURRENT_SOURCE_DIR}/content
    ${CMAKE_CURRENT_SOURCE_DIR}/worldgen)
target_link_libraries(qc_server PUBLIC qc_common qc_base_content Qt::Core Qt::Concurrent)
