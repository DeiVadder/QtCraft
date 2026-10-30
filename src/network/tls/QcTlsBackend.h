// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcResult.h"

/*! \brief Which TLS the program speaks (decision E-872): Qt's "openssl" backend on every platform,
    with the OpenSSL the game ships (scripts/build-openssl.sh).

    The libraries come with the program: every program that links the network layer loads libssl and
    libcrypto at its start (rpath; the Frameworks folder of the app bundle), and Qt's backend, which
    opens libssl by name, finds them loaded. A build with QC_TLS=OFF has no TLS: connections are
    plain TCP. */
namespace QcTlsBackend {

/*! True in a build with TLS (QC_TLS=ON). */
inline constexpr bool enabled = QC_TLS_ENABLED != 0;

/*! Makes "openssl" Qt's active TLS backend and checks that it works, once per process (later calls
    return the first result). Fails when Qt has no OpenSSL backend or cannot load the libraries;
    always succeeds without TLS. Called before the first TLS socket is made; programs call it at
    their start so a broken installation shows at once. */
[[nodiscard]] QcStatus ensure();

/*! Returns "OpenSSL 3.5.9 30 Sep 2026" or similar, the library in use; empty before ensure() or
    without TLS. */
[[nodiscard]] QString libraryVersion();

} // namespace QcTlsBackend
