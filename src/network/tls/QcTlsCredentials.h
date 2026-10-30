// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcResult.h"

#include <QByteArray>
#include <QSslCertificate>
#include <QSslKey>
#include <QString>

/*! \brief The certificate and private key a server shows in the TLS handshake (decision E-872): an
    EC key on P-256 from OpenSSL (QcTlsKey) and a self-signed certificate for it, whose structure
    this program writes in DER (QcDer), made once per installation and kept in its data folder next
    to server.json.

    No authority signs it; clients pin it at their first connection (QcKnownServers), so it must
    stay the same: it is valid until 9000. */
struct QcTlsCredentials
{
    /*! The names of the two files in the data folder. \qcshared */
    static constexpr char certificateFileName[] = "server-certificate.pem";
    static constexpr char keyFileName[] = "server-key.pem";
    /*! The subject of a new certificate. */
    static constexpr char defaultCommonName[] = "QtCraft server";

    /*! The certificate and its private key. Initialised explicitly: the default constructors of
        both are explicit, and the struct is part of aggregates built with {} (QcNetworkConfig).
        \qcshared */
    QSslCertificate certificate = QSslCertificate();
    QSslKey privateKey = QSslKey();

    /*! Returns true when the certificate or the key is missing. */
    [[nodiscard]] bool isNull() const { return certificate.isNull() || privateKey.isNull(); }

    /*! Returns a new key and its certificate for \a commonName; fails in a build without TLS. */
    [[nodiscard]] static QcResult<QcTlsCredentials> generate(const QString &commonName = QString::fromLatin1(defaultCommonName));
    /*! Returns the credentials in the PEM texts \a certificatePem and \a keyPem (the files, or a
        test's fixed credentials), or why they cannot be read. */
    [[nodiscard]] static QcResult<QcTlsCredentials> fromPem(const QByteArray &certificatePem, const QByteArray &keyPem);
    /*! Returns the two files in \a directory; when neither exists they are made and written, the
        key readable only by the user. A damaged or single file is an error and nothing is
        replaced: players have pinned the certificate, a new one would look like an attack to
        them. Without TLS (QcTlsBackend::enabled) empty credentials and no files. */
    [[nodiscard]] static QcResult<QcTlsCredentials> loadOrCreate(const QString &directory);
};

/*! \brief The fingerprint clients pin: SHA-256 of the certificate (DER), as lowercase hex. */
namespace QcTlsFingerprint {

/*! Returns the fingerprint of \a certificate. */
[[nodiscard]] QByteArray of(const QSslCertificate &certificate);
/*! Returns the fingerprint \a hex for people: upper case in groups of four, "AB12 CD34 …". */
[[nodiscard]] QString display(const QByteArray &hex);

} // namespace QcTlsFingerprint
