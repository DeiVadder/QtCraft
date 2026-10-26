# Network transport (decision E-251): TCP framing, connecting and server addresses. Common layer:
# used by the server's listener and by clients.
qt_add_library(qc_network ${QC_LIBRARY_TYPE})
# Sources sorted, each header next to its sources (decision E-417;
# scripts/check-cmake-lists.py checks the lists).
target_sources(qc_network PRIVATE
    QcNetworkConfig.h
    QcServerAddress.h QcServerAddress.cpp
    QcSocketErrors.h QcSocketErrors.cpp
    QcTcpConnector.h QcTcpConnector.cpp
    QcTcpTransport.h QcTcpTransport.cpp
)
# TLS (decision E-872): the server's own certificate and the listener, through OpenSSL
# (cmake/QcTls.cmake). Without QC_TLS the key is not made and connections stay plain TCP.
target_sources(qc_network PRIVATE
    tls/QcDer.h tls/QcDer.cpp
    tls/QcTlsBackend.h tls/QcTlsBackend.cpp
    tls/QcTlsCredentials.h tls/QcTlsCredentials.cpp
    tls/QcTlsKey.h
    tls/QcTlsListener.h tls/QcTlsListener.cpp
)
if(QC_TLS_ENABLED)
    target_sources(qc_network PRIVATE tls/QcTlsKey.cpp)
    # Public: every program with the network layer loads libssl at its start, where Qt's OpenSSL
    # backend finds it (QcTlsBackend).
    target_link_libraries(qc_network PUBLIC QcOpenSSL::SSL QcOpenSSL::Crypto)
    target_compile_definitions(qc_network PUBLIC QC_TLS_ENABLED=1)
else()
    target_sources(qc_network PRIVATE tls/QcTlsKey_other.cpp)
    target_compile_definitions(qc_network PUBLIC QC_TLS_ENABLED=0)
endif()
qc_configure_target(qc_network)

# Includes name the header by its path below src/ ("common/blockrules/QcRails.h", decision E-418):
# src/ is the only include directory a library passes on.
target_include_directories(qc_network PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(qc_network PUBLIC qc_common Qt::Core Qt::Network)
