# Network transport (decision E-251): TCP framing, connecting and server addresses. Common layer:
# used by the server's listener and by clients.
qt_add_library(qc_network STATIC
    QcServerAddress.h QcServerAddress.cpp
    QcSocketErrors.h QcSocketErrors.cpp
    QcTcpConnector.h QcTcpConnector.cpp
    QcTcpTransport.h QcTcpTransport.cpp
)
qc_configure_target(qc_network)
target_include_directories(qc_network PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qc_network PUBLIC qc_common Qt::Core Qt::Network)
