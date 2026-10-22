# Network transport (decision E-251): TCP framing, connecting and server addresses. Common layer:
# used by the server's listener and by clients.
qt_add_library(qc_network STATIC)
# Sources sorted, each header next to its sources (decision E-417;
# scripts/check-cmake-lists.py checks the lists).
target_sources(qc_network PRIVATE
    QcNetworkConfig.h
    QcServerAddress.h QcServerAddress.cpp
    QcSocketErrors.h QcSocketErrors.cpp
    QcTcpConnector.h QcTcpConnector.cpp
    QcTcpTransport.h QcTcpTransport.cpp
)
qc_configure_target(qc_network)
# Includes name the header by its path below src/ ("common/blockrules/QcRails.h", decision E-418):
# src/ is the only include directory a library passes on.
target_include_directories(qc_network PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(qc_network PUBLIC qc_common Qt::Core Qt::Network)
