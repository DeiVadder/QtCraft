# TLS in multiplayer through OpenSSL (decision E-872). The game uses Qt's "openssl" TLS backend on
# every platform and ships its own OpenSSL: the version of 3rdparty/openssl-version.txt, built once
# by scripts/build-openssl.sh into a cache shared by every preset and worktree
# (~/.cache/qtcraft/openssl/<version>-<arch>-<config>, "universal" on macOS). The first configure
# step builds it when the entry is missing (QC_OPENSSL_AUTO_BUILD); normal builds and verify.sh
# never do. QC_OPENSSL_ROOT points at another OpenSSL 3 (a prefix with include/ and lib/); its
# version must match the pinned one. The result are the imported targets QcOpenSSL::Crypto and
# QcOpenSSL::SSL and QC_OPENSSL_ROOT_FOUND (the prefix). Every program that links qc_network loads
# both libraries at its start (through the rpath), and Qt's OpenSSL backend, which loads libssl by
# name at run time, finds them there (QcTlsBackend).
#
# QC_TLS=OFF builds without OpenSSL: connections are plain TCP, not encrypted, and no certificate
# is pinned; only for platforms where OpenSSL is not available yet. Such a build cannot talk to one
# with TLS.
option(QC_TLS "Encrypt multiplayer connections with TLS through the bundled OpenSSL (off: plain TCP, not encrypted)" ON)
option(QC_OPENSSL_AUTO_BUILD "Run scripts/build-openssl.sh during configure when the OpenSSL cache entry is missing" ON)
set(QC_OPENSSL_ROOT "" CACHE PATH "Prefix of an OpenSSL 3 build to use instead of the shared cache entry")

set(QC_TLS_ENABLED OFF)
if(NOT QC_TLS)
    message(WARNING "QC_TLS is OFF: multiplayer connections are NOT encrypted (plain TCP), and this build cannot join servers with TLS")
    return()
endif()

set(_qc_openssl_script "${PROJECT_SOURCE_DIR}/scripts/build-openssl.sh")
set(_qc_openssl_version_file "${PROJECT_SOURCE_DIR}/3rdparty/openssl-version.txt")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_qc_openssl_version_file}" "${_qc_openssl_script}")
file(STRINGS "${_qc_openssl_version_file}" _qc_openssl_pin LIMIT_COUNT 1)
string(REGEX REPLACE " .*" "" _qc_openssl_wanted "${_qc_openssl_pin}")

if(APPLE)
    set(_qc_crypto_name libcrypto.3.dylib)
    set(_qc_ssl_name libssl.3.dylib)
elseif(WIN32)
    set(_qc_crypto_name "")
else()
    set(_qc_crypto_name libcrypto.so.3)
    set(_qc_ssl_name libssl.so.3)
endif()

set(_qc_openssl_prefix "${QC_OPENSSL_ROOT}")
if(NOT _qc_openssl_prefix AND NOT WIN32)
    find_program(QC_OPENSSL_SHELL sh)
    execute_process(COMMAND "${QC_OPENSSL_SHELL}" "${_qc_openssl_script}" --print-prefix
        OUTPUT_VARIABLE _qc_openssl_prefix OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT EXISTS "${_qc_openssl_prefix}/.complete" AND QC_OPENSSL_AUTO_BUILD)
        message(STATUS "OpenSSL missing in ${_qc_openssl_prefix}: building it once (scripts/build-openssl.sh, a few minutes)")
        find_program(_qc_nice nice)
        set(_qc_nice_command "")
        if(_qc_nice)
            set(_qc_nice_command "${_qc_nice}" -n 10)
        endif()
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E env QC_OPENSSL_WAIT_BENCH_LOCK=1 ${_qc_nice_command} "${QC_OPENSSL_SHELL}" "${_qc_openssl_script}"
            RESULT_VARIABLE _qc_openssl_result OUTPUT_QUIET)
    endif()
endif()

if(NOT _qc_crypto_name OR NOT EXISTS "${_qc_openssl_prefix}/lib/${_qc_crypto_name}" OR NOT EXISTS "${_qc_openssl_prefix}/lib/${_qc_ssl_name}")
    message(FATAL_ERROR
        "OpenSSL ${_qc_openssl_wanted} (shared libcrypto and libssl) not found in '${_qc_openssl_prefix}'.\n"
        "Build it once with:  scripts/build-openssl.sh\n"
        "or point QC_OPENSSL_ROOT at an OpenSSL ${_qc_openssl_wanted} prefix,\n"
        "or configure without encryption (plain TCP, not for play over the internet):  -DQC_TLS=OFF")
endif()

# The version the headers name must be the pinned one (a QC_OPENSSL_ROOT of another version would
# quietly change what players get).
file(STRINGS "${_qc_openssl_prefix}/include/openssl/opensslv.h" _qc_openssl_version_line REGEX "^# *define +OPENSSL_FULL_VERSION_STR")
string(REGEX MATCH "\"([0-9.]+)" _qc_openssl_found "${_qc_openssl_version_line}")
set(_qc_openssl_found "${CMAKE_MATCH_1}")
if(NOT _qc_openssl_found VERSION_EQUAL _qc_openssl_wanted)
    message(FATAL_ERROR
        "OpenSSL in ${_qc_openssl_prefix} is version '${_qc_openssl_found}', 3rdparty/openssl-version.txt wants ${_qc_openssl_wanted}.\n"
        "Unset QC_OPENSSL_ROOT to use the shared cache, or point it at an OpenSSL ${_qc_openssl_wanted} build.")
endif()

foreach(_qc_part IN ITEMS Crypto SSL)
    if(_qc_part STREQUAL "Crypto")
        set(_qc_file "${_qc_crypto_name}")
    else()
        set(_qc_file "${_qc_ssl_name}")
    endif()
    add_library(QcOpenSSL::${_qc_part} SHARED IMPORTED GLOBAL)
    set_target_properties(QcOpenSSL::${_qc_part} PROPERTIES
        IMPORTED_LOCATION "${_qc_openssl_prefix}/lib/${_qc_file}"
        INTERFACE_INCLUDE_DIRECTORIES "${_qc_openssl_prefix}/include")
    if(APPLE)
        set_target_properties(QcOpenSSL::${_qc_part} PROPERTIES IMPORTED_SONAME "@rpath/${_qc_file}")
    endif()
endforeach()
set_target_properties(QcOpenSSL::SSL PROPERTIES INTERFACE_LINK_LIBRARIES QcOpenSSL::Crypto)

set(QC_TLS_ENABLED ON)
set(QC_OPENSSL_ROOT_FOUND "${_qc_openssl_prefix}")
message(STATUS "TLS: OpenSSL ${_qc_openssl_found} from ${_qc_openssl_prefix}")
