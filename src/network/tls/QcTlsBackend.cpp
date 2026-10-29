// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "network/tls/QcTlsBackend.h"

#include "common/core/QcLogging.h"

#include <QCoreApplication>
#include <QSslSocket>

#if QC_TLS_ENABLED
#include <openssl/crypto.h>
#include <openssl/ssl.h>
#endif

using namespace Qt::StringLiterals;

namespace {

constexpr auto backendName = "openssl"_L1;

// OpenSSL reads its configuration once, at its first initialisation: this one comes before Qt's
// and reads none, so neither OPENSSL_CONF nor a file in OPENSSLDIR changes the protocols, ciphers
// or providers of the game (decision E-872).
bool initialiseWithoutConfiguration()
{
#if QC_TLS_ENABLED
    return OPENSSL_init_ssl(OPENSSL_INIT_NO_LOAD_CONFIG, nullptr) == 1;
#else
    return true;
#endif
}

QcStatus select()
{
    if (!QcTlsBackend::enabled) {
        qCWarning(lcConnection) << "This build has no TLS (QC_TLS=OFF): multiplayer connections are not encrypted";
        return {};
    }
    if (!initialiseWithoutConfiguration()) {
        return qcError(QCoreApplication::translate("QcNetwork", "The OpenSSL libraries of QtCraft cannot be initialised"));
    }
    if (QSslSocket::activeBackend() != backendName && !QSslSocket::setActiveBackend(backendName)) {
        return qcError(QCoreApplication::translate("QcNetwork", "Qt's OpenSSL backend is missing (TLS backends found: %1)")
                           .arg(QSslSocket::availableBackends().join(u", "_s)));
    }
    if (!QSslSocket::supportsSsl()) {
        return qcError(QCoreApplication::translate("QcNetwork", "The OpenSSL libraries of QtCraft cannot be loaded (built for %1)")
                           .arg(QSslSocket::sslLibraryBuildVersionString()));
    }
    qCInfo(lcConnection).noquote() << u"TLS through %1"_s.arg(QSslSocket::sslLibraryVersionString());
    return {};
}

} // namespace

namespace QcTlsBackend {

QcStatus ensure()
{
    // A function-local static: made once, safe against concurrent first calls.
    static const QcStatus result = [] {
        QcStatus selected = select();
        if (!selected) {
            qCCritical(lcConnection).noquote() << u"No TLS, multiplayer is not possible: %1"_s.arg(selected.error());
        }
        return selected;
    }();
    return result;
}

QString libraryVersion()
{
    if (!enabled || QSslSocket::activeBackend() != backendName) {
        return {};
    }
    return QSslSocket::sslLibraryVersionString();
}

} // namespace QcTlsBackend
