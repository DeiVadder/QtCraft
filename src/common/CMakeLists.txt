qt_add_library(qc_common STATIC
    QcAabb.h
    QcBlockAccess.h
    QcBlockDefinition.h
    QcBlockFace.h
    QcBlockPos.h QcBlockPos.cpp
    QcChunk.h QcChunk.cpp
    QcChunkPos.h QcChunkPos.cpp
    QcChunkSection.h QcChunkSection.cpp
    QcConnection.h QcConnection.cpp
    QcContentNames.h QcContentNames.cpp
    QcCrc32.h QcCrc32.cpp
    QcDayCycle.h QcDayCycle.cpp
    QcContentParser.h QcContentParser.cpp
    QcContentRegistry.h QcContentRegistry.cpp
    QcEntityMotion.h
    QcInProcessTransport.h QcInProcessTransport.cpp
    QcIntMath.h
    QcInventory.h QcInventory.cpp
    QcItemDefinition.h
    QcItemStack.h
    QcMailbox.h
    QcMessages.h
    QcMining.h QcMining.cpp
    QcMobDefinition.h
    QcPhysics.h QcPhysics.cpp
    QcPlayerInput.h
    QcProtocol.h QcProtocol.cpp
    QcRaycast.h QcRaycast.cpp
    QcResult.h
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
