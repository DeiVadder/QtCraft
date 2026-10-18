# World generation (decision E-418): terrain, structures, villages and the showcase as pure
# functions of the seed and the content; depends on qc_common only, so the generation tests do not
# link the server.
qt_add_library(qc_worldgen STATIC)
# Sources per folder, sorted, each header next to its sources (decision E-417;
# scripts/check-cmake-lists.py checks the lists).
target_sources(qc_worldgen PRIVATE
    dungeons/QcDungeonGenerator.h dungeons/QcDungeonGenerator.cpp
    dungeons/QcDungeonPlan.h
    dungeons/QcDungeonPlanner.h dungeons/QcDungeonPlanner.cpp
)
target_sources(qc_worldgen PRIVATE
    generators/QcChunkFurnishing.h
    generators/QcFlatGenerator.h generators/QcFlatGenerator.cpp
    generators/QcTestTerrainGenerator.h generators/QcTestTerrainGenerator.cpp
    generators/QcVoidGenerator.h generators/QcVoidGenerator.cpp
    generators/QcWorldGenerator.h
    generators/QcWorldGenerators.h generators/QcWorldGenerators.cpp
    generators/QcWorldSetup.h
)
target_sources(qc_worldgen PRIVATE
    mineshafts/QcMineshaftAccess.h mineshafts/QcMineshaftAccess.cpp
    mineshafts/QcMineshaftBlocks.h mineshafts/QcMineshaftBlocks.cpp
    mineshafts/QcMineshaftBuilder.h mineshafts/QcMineshaftBuilder.cpp
    mineshafts/QcMineshaftDetails.h mineshafts/QcMineshaftDetails.cpp
    mineshafts/QcMineshaftFrame.h
    mineshafts/QcMineshaftGenerator.h mineshafts/QcMineshaftGenerator.cpp
    mineshafts/QcMineshaftNetwork.h mineshafts/QcMineshaftNetwork.cpp
    mineshafts/QcMineshaftPlan.h
    mineshafts/QcMineshaftPlanner.h mineshafts/QcMineshaftPlanner.cpp
    mineshafts/QcMineshaftSite.h mineshafts/QcMineshaftSite.cpp
)
target_sources(qc_worldgen PRIVATE
    showcase/QcShowcaseArea.h
    showcase/QcShowcaseArmourArea.h showcase/QcShowcaseArmourArea.cpp
    showcase/QcShowcaseBlockArea.h showcase/QcShowcaseBlockArea.cpp
    showcase/QcShowcaseCategories.h showcase/QcShowcaseCategories.cpp
    showcase/QcShowcaseCreatureArea.h showcase/QcShowcaseCreatureArea.cpp
    showcase/QcShowcaseGenerator.h showcase/QcShowcaseGenerator.cpp
    showcase/QcShowcaseItemArea.h showcase/QcShowcaseItemArea.cpp
    showcase/QcShowcasePlan.h
    showcase/QcShowcasePlanner.h showcase/QcShowcasePlanner.cpp
    showcase/QcShowcaseSettlement.h showcase/QcShowcaseSettlement.cpp
    showcase/QcShowcaseTreeArea.h showcase/QcShowcaseTreeArea.cpp
    showcase/QcShowcaseVillageArea.h showcase/QcShowcaseVillageArea.cpp
)
target_sources(qc_worldgen PRIVATE
    structures/QcStateRotation.h structures/QcStateRotation.cpp
    structures/QcStructure.h structures/QcStructure.cpp
    structures/QcStructurePlacement.h structures/QcStructurePlacement.cpp
)
target_sources(qc_worldgen PRIVATE
    terrain/QcCaveCarver.h terrain/QcCaveCarver.cpp
    terrain/QcNoise.h terrain/QcNoise.cpp
    terrain/QcPoolPlacer.h terrain/QcPoolPlacer.cpp
    terrain/QcTerrainColumn.h
    terrain/QcTerrainColumnGrid.h terrain/QcTerrainColumnGrid.cpp
    terrain/QcTerrainDefinitions.h terrain/QcTerrainDefinitions.cpp
    terrain/QcTerrainGenerator.h terrain/QcTerrainGenerator.cpp
    terrain/QcTerrainMath.h
    terrain/QcTreeShape.h terrain/QcTreeShape.cpp
)
target_sources(qc_worldgen PRIVATE
    villages/QcBoardwalkPlanner.h villages/QcBoardwalkPlanner.cpp
    villages/QcJettyPlanner.h villages/QcJettyPlanner.cpp
    villages/QcPiecePlacer.h villages/QcPiecePlacer.cpp
    villages/QcPieceVariation.h villages/QcPieceVariation.cpp
    villages/QcRoadLayer.h villages/QcRoadLayer.cpp
    villages/QcRoadNetworkPlanner.h villages/QcRoadNetworkPlanner.cpp
    villages/QcVillageBlocks.h villages/QcVillageBlocks.cpp
    villages/QcVillageDraft.h villages/QcVillageDraft.cpp
    villages/QcVillageFields.h villages/QcVillageFields.cpp
    villages/QcVillageForecourt.h villages/QcVillageForecourt.cpp
    villages/QcVillageGenerator.h villages/QcVillageGenerator.cpp
    villages/QcVillagePlan.h
    villages/QcVillagePlanCache.h villages/QcVillagePlanCache.cpp
    villages/QcVillagePlanner.h villages/QcVillagePlanner.cpp
    villages/QcVillageRoads.h villages/QcVillageRoads.cpp
    villages/QcVillageStyles.h villages/QcVillageStyles.cpp
    villages/QcWetlandShaper.h villages/QcWetlandShaper.cpp
)
qc_configure_target(qc_worldgen)
# Identical terrain on every compiler and CPU: no fused multiply-add contraction in noise maths.
set(qc_worldgen_fp_sources
    terrain/QcCaveCarver.cpp terrain/QcNoise.cpp terrain/QcPoolPlacer.cpp terrain/QcTerrainColumnGrid.cpp
    terrain/QcTerrainDefinitions.cpp terrain/QcTerrainGenerator.cpp terrain/QcTreeShape.cpp
    villages/QcBoardwalkPlanner.cpp villages/QcJettyPlanner.cpp villages/QcPiecePlacer.cpp villages/QcRoadLayer.cpp
    villages/QcRoadNetworkPlanner.cpp villages/QcVillageDraft.cpp villages/QcVillageForecourt.cpp villages/QcVillagePlanner.cpp
    villages/QcWetlandShaper.cpp)
if(MSVC)
    set_source_files_properties(${qc_worldgen_fp_sources} PROPERTIES COMPILE_OPTIONS /fp:precise)
else()
    set_source_files_properties(${qc_worldgen_fp_sources} PROPERTIES COMPILE_OPTIONS -ffp-contract=off)
endif()
# Includes name the header by its path below src/ ("server/blocks/QcRails.h", decision E-418):
# src/ is the only include directory a library passes on.
target_include_directories(qc_worldgen PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(qc_worldgen PUBLIC qc_common Qt::Core)
