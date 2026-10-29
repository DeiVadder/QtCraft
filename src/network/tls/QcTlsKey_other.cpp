// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "network/tls/QcTlsKey.h"

#include <QCoreApplication>

// A build without TLS (QC_TLS=OFF, decision E-872) has no OpenSSL and makes no keys; its servers
// listen without certificate.

QcResult<QcTlsKey> QcTlsKey::generate()
{
    return qcError(QCoreApplication::translate("QcNetwork", "This build of QtCraft has no encryption (QC_TLS=OFF) and makes no server key"));
}

QByteArray QcTlsKey::subjectPublicKeyInfoDer() const
{
    return {};
}

QByteArray QcTlsKey::signatureAlgorithmDer()
{
    return {};
}

QByteArray QcTlsKey::privateKeyPem() const
{
    return {};
}

QcResult<QByteArray> QcTlsKey::sign(const QByteArray &message) const
{
    Q_UNUSED(message)
    return qcError(QCoreApplication::translate("QcNetwork", "The server's certificate cannot be signed: %1").arg(QStringLiteral("QC_TLS=OFF")));
}

bool QcTlsKey::verify(const QByteArray &message, const QByteArray &signature) const
{
    Q_UNUSED(message)
    Q_UNUSED(signature)
    return false;
}

bool QcTlsKey::belongTogether(const QByteArray &keyPem, const QByteArray &certificateDer)
{
    Q_UNUSED(keyPem)
    Q_UNUSED(certificateDer)
    return false;
}
