# World generation (decision E-418): terrain, structures, villages and the showcase as pure
# functions of the seed and the content; depends on qc_common only, so the generation tests do not
# link the server.
qt_add_library(qc_worldgen ${QC_LIBRARY_TYPE})
# Sources per folder, sorted, each header next to its sources (decision E-417;
# scripts/check-cmake-lists.py checks the lists).
target_sources(qc_worldgen PRIVATE
    dungeons/QcDungeonGenerator.h dungeons/QcDungeonGenerator.cpp
    dungeons/QcDungeonPlan.h
    dungeons/QcDungeonPlanner.h dungeons/QcDungeonPlanner.cpp
)
target_sources(qc_worldgen PRIVATE
    generators/QcBiomeMap.h generators/QcBiomeMap.cpp
    generators/QcChunkFurnishing.h
    generators/QcFlatGenerator.h generators/QcFlatGenerator.cpp
    generators/QcGuardSite.h
    generators/QcLocatedPlace.h
    generators/QcStructureSites.h
    generators/QcTestTerrainGenerator.h generators/QcTestTerrainGenerator.cpp
    generators/QcVoidGenerator.h generators/QcVoidGenerator.cpp
    generators/QcWorldGenerator.h
    generators/QcWorldGenerators.h generators/QcWorldGenerators.cpp
    generators/QcWorldgenLogging.h generators/QcWorldgenLogging.cpp
    generators/QcWorldOptions.h generators/QcWorldOptions.cpp
    generators/QcWorldOptionTargets.h generators/QcWorldOptionTargets.cpp
    generators/QcWorldSetup.h
)
target_sources(qc_worldgen PRIVATE
    mineshafts/QcMineshaftAccess.h mineshafts/QcMineshaftAccess.cpp
    mineshafts/QcMineshaftBlocks.h mineshafts/QcMineshaftBlocks.cpp
    mineshafts/QcMineshaftBuilder.h mineshafts/QcMineshaftBuilder.cpp
    mineshafts/QcMineshaftDetails.h mineshafts/QcMineshaftDetails.cpp
    mineshafts/QcMineshaftFrame.h
    mineshafts/QcMineshaftGenerator.h mineshafts/QcMineshaftGenerator.cpp
    mineshafts/QcMineshaftIncline.h mineshafts/QcMineshaftIncline.cpp
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
    surface/QcSurfaceBlend.h surface/QcSurfaceBlend.cpp
    surface/QcSurfacePlanner.h surface/QcSurfacePlanner.cpp
    surface/QcSurfaceStructures.h surface/QcSurfaceStructures.cpp
)
target_sources(qc_worldgen PRIVATE
    terrain/QcCaveCarver.h terrain/QcCaveCarver.cpp
    terrain/QcClimateBiomes.h terrain/QcClimateBiomes.cpp
    terrain/QcColdEdges.h terrain/QcColdEdges.cpp
    terrain/QcDeltaArms.h terrain/QcDeltaArms.cpp
    terrain/QcDeltaSites.h terrain/QcDeltaSites.cpp
    terrain/QcGiantTreePlacer.h terrain/QcGiantTreePlacer.cpp
    terrain/QcNoise.h terrain/QcNoise.cpp
    terrain/QcPoolPlacer.h terrain/QcPoolPlacer.cpp
    terrain/QcRiverDelta.h terrain/QcRiverDelta.cpp
    terrain/QcRiverField.h terrain/QcRiverField.cpp
    terrain/QcRiverPieces.h terrain/QcRiverPieces.cpp
    terrain/QcSurfaceLakes.h terrain/QcSurfaceLakes.cpp
    terrain/QcTerrainClimate.h terrain/QcTerrainClimate.cpp
    terrain/QcTerrainColumn.h
    terrain/QcTerrainColumnGrid.h terrain/QcTerrainColumnGrid.cpp
    terrain/QcTerrainDefinitions.h terrain/QcTerrainDefinitions.cpp
    terrain/QcTerrainGenerator.h terrain/QcTerrainGenerator.cpp
    terrain/QcTerrainGround.cpp
    terrain/QcTerrainLocate.cpp
    terrain/QcTerrainMath.h
    terrain/QcTerrainRivers.cpp
    terrain/QcTerrainSpawn.cpp
    terrain/QcTerrainSurface.h terrain/QcTerrainSurface.cpp
    terrain/QcTreeLayout.h terrain/QcTreeLayout.cpp
    terrain/QcTreeShape.h terrain/QcTreeShape.cpp
    terrain/QcVegetationPatchPlacer.h terrain/QcVegetationPatchPlacer.cpp
)
target_sources(qc_worldgen PRIVATE
    towers/QcCoastalTowerGenerator.h towers/QcCoastalTowerGenerator.cpp
    towers/QcCoastalTowerPlan.h
    towers/QcCoastalTowerPlanner.h towers/QcCoastalTowerPlanner.cpp
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
    villages/QcWetlandPattern.h villages/QcWetlandPattern.cpp
    villages/QcWetlandShaper.h villages/QcWetlandShaper.cpp
)
qc_configure_target(qc_worldgen)
# Identical terrain on every compiler and CPU: no fused multiply-add contraction in noise maths.
set(qc_worldgen_fp_sources
    terrain/QcCaveCarver.cpp terrain/QcClimateBiomes.cpp terrain/QcColdEdges.cpp terrain/QcDeltaArms.cpp terrain/QcDeltaSites.cpp terrain/QcGiantTreePlacer.cpp terrain/QcNoise.cpp terrain/QcPoolPlacer.cpp
    terrain/QcRiverDelta.cpp terrain/QcRiverField.cpp terrain/QcRiverPieces.cpp terrain/QcSurfaceLakes.cpp terrain/QcTerrainClimate.cpp terrain/QcTerrainColumnGrid.cpp terrain/QcTerrainDefinitions.cpp
    terrain/QcTerrainGenerator.cpp terrain/QcTerrainGround.cpp terrain/QcTerrainRivers.cpp terrain/QcTerrainSurface.cpp terrain/QcTreeLayout.cpp terrain/QcTreeShape.cpp
    terrain/QcVegetationPatchPlacer.cpp villages/QcBoardwalkPlanner.cpp villages/QcJettyPlanner.cpp villages/QcPiecePlacer.cpp villages/QcRoadLayer.cpp
    villages/QcRoadNetworkPlanner.cpp villages/QcVillageDraft.cpp villages/QcVillageForecourt.cpp villages/QcVillagePlanner.cpp
    villages/QcWetlandPattern.cpp villages/QcWetlandShaper.cpp)
if(MSVC)
    set_source_files_properties(${qc_worldgen_fp_sources} PROPERTIES COMPILE_OPTIONS /fp:precise)
else()
    set_source_files_properties(${qc_worldgen_fp_sources} PROPERTIES COMPILE_OPTIONS -ffp-contract=off)
    # The noise is optimised in debug and sanitizer builds too (decision E-900): unoptimised, its
    # small helpers and the checked std::array access took three quarters of the time of every
    # terrain test. Without fast-math and with contraction off the results stay bit for bit those of
    # the release build.
    set_property(SOURCE terrain/QcNoise.cpp APPEND PROPERTY COMPILE_OPTIONS "$<$<CONFIG:Debug>:-O2>")
endif()
# Includes name the header by its path below src/ ("common/blockrules/QcRails.h", decision E-418):
# src/ is the only include directory a library passes on.
target_include_directories(qc_worldgen PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(qc_worldgen PUBLIC qc_common Qt::Core)
