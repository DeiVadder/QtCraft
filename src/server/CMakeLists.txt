qt_add_library(qc_server STATIC
    QcServer.h QcServer.cpp
)
qc_configure_target(qc_server)
target_include_directories(qc_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qc_server PUBLIC qc_common Qt::Core Qt::Concurrent)
