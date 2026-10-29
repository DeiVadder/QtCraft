# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# TLS in multiplayer through OpenSSL (decision E-872). The game uses Qt's "openssl" TLS backend on
# every platform. macOS and Windows ship their own OpenSSL: the version of
# 3rdparty/openssl-version.txt, built once into a cache shared by every preset and checkout, by
# scripts/build-openssl.sh on macOS (~/.cache/qtcraft/openssl/<version>-universal-<config>) and by
# scripts/windows/build-openssl.ps1 on Windows (%USERPROFILE%\.cache\qtcraft\openssl\<version>-<x64|arm64>-<config>).
# The first configure step builds it when the entry is missing (QC_OPENSSL_AUTO_BUILD); normal
# builds and verify.sh never do. Linux uses the OpenSSL 3 of the system (find_package; the
# distribution keeps it patched, and Qt's backend there loads the system libssl.so.3 anyway).
# QC_OPENSSL_ROOT points at another OpenSSL 3 (a prefix with include/ and lib/, on Windows also
# bin/ with the DLLs) on every platform; its version must match the pinned one. The result are the
# imported targets QcOpenSSL::Crypto and QcOpenSSL::SSL and QC_OPENSSL_ROOT_FOUND (the prefix; empty
# for the system OpenSSL). Every program that links qc_network loads both libraries at its start
# (macOS: through the rpath; Windows: the DLLs next to the executable, copied into the build tree's
# runtime folder here and into the package by scripts/windows/package-windows.ps1), and Qt's
# OpenSSL backend, which loads libssl by name at run time, finds them there (QcTlsBackend).
#
# QC_TLS=OFF builds without OpenSSL: connections are plain TCP, not encrypted, and no certificate
# is pinned; only for platforms where OpenSSL is not available yet. Such a build cannot talk to one
# with TLS.
option(QC_TLS "Encrypt multiplayer connections with TLS through the bundled OpenSSL (off: plain TCP, not encrypted)" ON)
option(QC_OPENSSL_AUTO_BUILD "Run scripts/build-openssl.sh (Windows: scripts/windows/build-openssl.ps1) during configure when the OpenSSL cache entry is missing" ON)
set(QC_OPENSSL_ROOT "" CACHE PATH "Prefix of an OpenSSL 3 build to use instead of the shared cache entry")

set(QC_TLS_ENABLED OFF)
# Set again below only for a bundled OpenSSL; a stale value would name a prefix the build no longer uses.
unset(QC_OPENSSL_PREFIX CACHE)
if(NOT QC_TLS)
    message(WARNING "QC_TLS is OFF: multiplayer connections are NOT encrypted (plain TCP), and this build cannot join servers with TLS")
    return()
endif()

set(_qc_openssl_version_file "${PROJECT_SOURCE_DIR}/3rdparty/openssl-version.txt")
file(STRINGS "${_qc_openssl_version_file}" _qc_openssl_pin LIMIT_COUNT 1)
string(REGEX REPLACE " .*" "" _qc_openssl_wanted "${_qc_openssl_pin}")

# Linux without QC_OPENSSL_ROOT: the system's OpenSSL 3, any 3.x but not 4 (its headers and
# libraries belong together; test_tls_credentials checks that the loaded library is the one of the
# headers).
if(NOT APPLE AND NOT WIN32 AND NOT QC_OPENSSL_ROOT)
    find_package(OpenSSL 3...<4 COMPONENTS Crypto SSL)
    if(NOT OpenSSL_FOUND)
        message(FATAL_ERROR
            "OpenSSL 3 of the system (headers and shared libraries) not found.\n"
            "Install it (Debian/Ubuntu: libssl-dev, Fedora: openssl-devel),\n"
            "or point QC_OPENSSL_ROOT at an OpenSSL ${_qc_openssl_wanted} prefix (scripts/build-openssl.sh builds one),\n"
            "or configure without encryption (plain TCP, not for play over the internet):  -DQC_TLS=OFF")
    endif()
    add_library(QcOpenSSL::Crypto INTERFACE IMPORTED GLOBAL)
    set_target_properties(QcOpenSSL::Crypto PROPERTIES INTERFACE_LINK_LIBRARIES OpenSSL::Crypto)
    add_library(QcOpenSSL::SSL INTERFACE IMPORTED GLOBAL)
    set_target_properties(QcOpenSSL::SSL PROPERTIES INTERFACE_LINK_LIBRARIES "OpenSSL::SSL;QcOpenSSL::Crypto")
    set(QC_TLS_ENABLED ON)
    set(QC_OPENSSL_ROOT_FOUND "")
    message(STATUS "TLS: OpenSSL ${OPENSSL_VERSION} of the system (${OPENSSL_SSL_LIBRARY})")
    return()
endif()

# File names: the shared libraries (Windows: the DLLs in bin/, named as Qt's backend loads them,
# and the import libraries in lib/).
set(_qc_openssl_runtime_dir lib)
if(APPLE)
    set(_qc_openssl_script "${PROJECT_SOURCE_DIR}/scripts/build-openssl.sh")
    set(_qc_crypto_name libcrypto.3.dylib)
    set(_qc_ssl_name libssl.3.dylib)
elseif(WIN32)
    set(_qc_openssl_script "${PROJECT_SOURCE_DIR}/scripts/windows/build-openssl.ps1")
    # The two targets the script builds (MSVC names them x64 and ARM64).
    if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID MATCHES "^(ARM64|arm64)$")
        set(_qc_openssl_arch arm64)
    elseif(CMAKE_CXX_COMPILER_ARCHITECTURE_ID MATCHES "^(x64|X64|AMD64|x86_64)$")
        set(_qc_openssl_arch x64)
    else()
        message(FATAL_ERROR
            "The bundled OpenSSL is built for x64 and ARM64 only, not for '${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}'.\n"
            "Point QC_OPENSSL_ROOT at an OpenSSL ${_qc_openssl_wanted} prefix, or configure with -DQC_TLS=OFF.")
    endif()
    set(_qc_openssl_runtime_dir bin)
    set(_qc_crypto_name libcrypto-3-${_qc_openssl_arch}.dll)
    set(_qc_ssl_name libssl-3-${_qc_openssl_arch}.dll)
else()
    set(_qc_openssl_script "${PROJECT_SOURCE_DIR}/scripts/build-openssl.sh")
    set(_qc_crypto_name libcrypto.so.3)
    set(_qc_ssl_name libssl.so.3)
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_qc_openssl_version_file}" "${_qc_openssl_script}")

# The cache directory comes from the script, so CMake and the script cannot disagree about it.
set(_qc_openssl_prefix "${QC_OPENSSL_ROOT}")
if(NOT _qc_openssl_prefix)
    find_program(_qc_nice nice)
    set(_qc_nice_command "")
    if(WIN32)
        find_program(QC_OPENSSL_POWERSHELL NAMES powershell pwsh)
        set(_qc_openssl_command "${QC_OPENSSL_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass -File "${_qc_openssl_script}" -Arch ${_qc_openssl_arch})
        set(_qc_openssl_print ${_qc_openssl_command} -PrintPrefix)
    else()
        find_program(QC_OPENSSL_SHELL sh)
        set(_qc_openssl_command "${QC_OPENSSL_SHELL}" "${_qc_openssl_script}")
        set(_qc_openssl_print ${_qc_openssl_command} --print-prefix)
        if(_qc_nice)
            set(_qc_nice_command "${_qc_nice}" -n 10)
        endif()
    endif()
    execute_process(COMMAND ${_qc_openssl_print} OUTPUT_VARIABLE _qc_openssl_prefix
        RESULT_VARIABLE _qc_openssl_print_result OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _qc_openssl_print_result EQUAL 0 OR NOT _qc_openssl_prefix)
        message(FATAL_ERROR
            "${_qc_openssl_script} could not name its cache directory (result '${_qc_openssl_print_result}').\n"
            "Run it by hand with -PrintPrefix (Windows, in PowerShell) or --print-prefix to see why,\n"
            "or point QC_OPENSSL_ROOT at an OpenSSL ${_qc_openssl_wanted} prefix.")
    endif()
    file(TO_CMAKE_PATH "${_qc_openssl_prefix}" _qc_openssl_prefix)
    if(NOT EXISTS "${_qc_openssl_prefix}/.complete" AND QC_OPENSSL_AUTO_BUILD)
        message(STATUS "OpenSSL missing in ${_qc_openssl_prefix}: building it once (${_qc_openssl_script}, a few minutes)")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E env QC_OPENSSL_WAIT_BENCH_LOCK=1 ${_qc_nice_command} ${_qc_openssl_command}
            RESULT_VARIABLE _qc_openssl_result OUTPUT_QUIET)
    endif()
endif()

if(NOT EXISTS "${_qc_openssl_prefix}/${_qc_openssl_runtime_dir}/${_qc_crypto_name}"
        OR NOT EXISTS "${_qc_openssl_prefix}/${_qc_openssl_runtime_dir}/${_qc_ssl_name}")
    file(RELATIVE_PATH _qc_openssl_script_name "${PROJECT_SOURCE_DIR}" "${_qc_openssl_script}")
    message(FATAL_ERROR
        "OpenSSL ${_qc_openssl_wanted} (shared libcrypto and libssl) not found in '${_qc_openssl_prefix}'.\n"
        "Build it once with:  ${_qc_openssl_script_name}\n"
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
        set(_qc_implib libcrypto.lib)
    else()
        set(_qc_file "${_qc_ssl_name}")
        set(_qc_implib libssl.lib)
    endif()
    add_library(QcOpenSSL::${_qc_part} SHARED IMPORTED GLOBAL)
    set_target_properties(QcOpenSSL::${_qc_part} PROPERTIES
        IMPORTED_LOCATION "${_qc_openssl_prefix}/${_qc_openssl_runtime_dir}/${_qc_file}"
        INTERFACE_INCLUDE_DIRECTORIES "${_qc_openssl_prefix}/include")
    if(APPLE)
        set_target_properties(QcOpenSSL::${_qc_part} PROPERTIES IMPORTED_SONAME "@rpath/${_qc_file}")
    elseif(WIN32)
        set_target_properties(QcOpenSSL::${_qc_part} PROPERTIES IMPORTED_IMPLIB "${_qc_openssl_prefix}/lib/${_qc_implib}")
    endif()
endforeach()
set_target_properties(QcOpenSSL::SSL PROPERTIES INTERFACE_LINK_LIBRARIES QcOpenSSL::Crypto)

# Windows has no rpath: the DLLs go next to the programs and tests of the build tree (all in the
# runtime output folder), where the loader and Qt's backend find them first.
# CMAKE_RUNTIME_OUTPUT_DIRECTORY comes from qt_standard_project_setup (the top-level
# CMakeLists.txt calls it before including this file).
if(WIN32)
    set(_qc_openssl_bin "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    if(NOT _qc_openssl_bin)
        set(_qc_openssl_bin "${CMAKE_BINARY_DIR}")
    endif()
    foreach(_qc_file IN ITEMS "${_qc_crypto_name}" "${_qc_ssl_name}")
        file(COPY_FILE "${_qc_openssl_prefix}/bin/${_qc_file}" "${_qc_openssl_bin}/${_qc_file}" ONLY_IF_DIFFERENT)
    endforeach()
endif()

set(QC_TLS_ENABLED ON)
set(QC_OPENSSL_ROOT_FOUND "${_qc_openssl_prefix}")
# Also in the cache: scripts/windows/package-windows.ps1 takes the licence text from there.
set(QC_OPENSSL_PREFIX "${_qc_openssl_prefix}" CACHE INTERNAL "Prefix of the OpenSSL the build uses (E-872)")
message(STATUS "TLS: OpenSSL ${_qc_openssl_found} from ${_qc_openssl_prefix}")
