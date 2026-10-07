qt_add_library(qc_common STATIC
    QcWorldConstants.h
    QcIntMath.h
    QcChunkPos.h QcChunkPos.cpp
    QcBlockPos.h QcBlockPos.cpp
)
qc_configure_target(qc_common)
target_include_directories(qc_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qc_common PUBLIC Qt::Core)
