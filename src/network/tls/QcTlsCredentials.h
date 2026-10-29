// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcResult.h"

#include <QByteArray>
#include <QSslCertificate>
#include <QSslKey>
#include <QString>

// The certificate and private key a server shows in the TLS handshake (decision E-872): an EC key
// on P-256 from OpenSSL (QcTlsKey) and a self-signed certificate for it, whose structure this
// program writes in DER (QcDer), made once per installation and kept in its data folder next to
// server.json. No authority signs it; clients pin it at their first connection
// (QcKnownServers), so it must stay the same: it is valid until 9000.
struct QcTlsCredentials
{
    static constexpr char certificateFileName[] = "server-certificate.pem";
    static constexpr char keyFileName[] = "server-key.pem";
    static constexpr char defaultCommonName[] = "QtCraft server";

    // Initialised explicitly: the default constructors of both are explicit, and the struct is
    // part of aggregates built with {} (QcNetworkConfig).
    QSslCertificate certificate = QSslCertificate();
    QSslKey privateKey = QSslKey();

    [[nodiscard]] bool isNull() const { return certificate.isNull() || privateKey.isNull(); }

    // A new key and its certificate; fails in a build without TLS.
    [[nodiscard]] static QcResult<QcTlsCredentials> generate(const QString &commonName = QString::fromLatin1(defaultCommonName));
    // From PEM text (the files, or a test's fixed credentials).
    [[nodiscard]] static QcResult<QcTlsCredentials> fromPem(const QByteArray &certificatePem, const QByteArray &keyPem);
    // The two files in `directory`; when neither exists they are made and written, the key readable
    // only by the user. A damaged or single file is an error and nothing is replaced: players have
    // pinned the certificate, a new one would look like an attack to them. Without TLS
    // (QcTlsBackend::enabled) empty credentials and no files.
    [[nodiscard]] static QcResult<QcTlsCredentials> loadOrCreate(const QString &directory);
};

// The fingerprint clients pin: SHA-256 of the certificate (DER), as lowercase hex.
namespace QcTlsFingerprint {

[[nodiscard]] QByteArray of(const QSslCertificate &certificate);
// For people: upper case in groups of four, "AB12 CD34 …".
[[nodiscard]] QString display(const QByteArray &hex);

} // namespace QcTlsFingerprint
