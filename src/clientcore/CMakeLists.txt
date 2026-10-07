qt_add_library(qc_clientcore STATIC
    QcBlockVertex.h
    QcCamera.h QcCamera.cpp
    QcChunkMesh.h
    QcTextureSpec.h
    QcTextureSynthesizer.h QcTextureSynthesizer.cpp
)
qc_configure_target(qc_clientcore)
target_include_directories(qc_clientcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qc_clientcore PUBLIC qc_common Qt::Core Qt::Gui)
