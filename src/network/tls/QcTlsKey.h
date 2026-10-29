// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcResult.h"

#include <QByteArray>
#include <QSsl>

#include <memory>

struct evp_pkey_st;

// The key of a server's certificate (decision E-872): an EC key on the curve P-256 (prime256v1),
// made and used by OpenSSL's libcrypto (EVP_PKEY_Q_keygen, EVP_DigestSign with SHA-256); the
// program implements no key generation or signatures of its own. The key lives in memory and in
// the key file only. A build without TLS (QC_TLS=OFF) has no implementation: generate() fails there
// with a translated reason.
class QcTlsKey
{
public:
    // The kind of key, as Qt reads it (QSslKey).
    static constexpr QSsl::KeyAlgorithm algorithm = QSsl::Ec;

    // A new key pair.
    [[nodiscard]] static QcResult<QcTlsKey> generate();

    // SubjectPublicKeyInfo (RFC 5280, 4.1.2.7) in DER, for the certificate.
    [[nodiscard]] QByteArray subjectPublicKeyInfoDer() const;
    // The AlgorithmIdentifier of sign() in DER: ecdsa-with-SHA256 (RFC 5758, 3.2).
    [[nodiscard]] static QByteArray signatureAlgorithmDer();
    // The private key as PKCS #8 PEM ("PRIVATE KEY"): the secret, only for the key file.
    [[nodiscard]] QByteArray privateKeyPem() const;
    // ECDSA with SHA-256 over `message`; the signature is an Ecdsa-Sig-Value in DER.
    [[nodiscard]] QcResult<QByteArray> sign(const QByteArray &message) const;
    // Whether `signature` is this key's signature of `message` (tests).
    [[nodiscard]] bool verify(const QByteArray &message, const QByteArray &signature) const;
    // Whether the private key in `keyPem` is the key of the certificate in `certificateDer`
    // (X509_check_private_key: the public keys are equal); false when either cannot be read.
    [[nodiscard]] static bool belongTogether(const QByteArray &keyPem, const QByteArray &certificateDer);

private:
    QcTlsKey() = default;

    // OpenSSL's key object, freed with the last copy.
    std::shared_ptr<evp_pkey_st> m_key;
};
