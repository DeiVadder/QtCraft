#include "network/tls/QcTlsCredentials.h"

#include "common/auth/QcSecretFile.h"
#include "common/core/QcLogging.h"
#include "network/tls/QcDer.h"
#include "network/tls/QcTlsBackend.h"
#include "network/tls/QcTlsKey.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QRandomGenerator>
#include <QTimeZone>

using namespace Qt::StringLiterals;

namespace {

// A name with one common name (RDNSequence of one attribute).
QByteArray name(const QString &commonName)
{
    return QcDer::sequence({QcDer::set({QcDer::sequence({QcDer::objectIdentifier(QcDer::commonName), QcDer::utf8String(commonName)})})});
}

// A positive serial number of 16 random bytes (RFC 5280 4.1.2.2: at most 20 bytes). It only has to
// be unique; it is no secret.
QByteArray serialNumber()
{
    QByteArray bytes(16, '\0');
    for (qsizetype i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<char>(QRandomGenerator::system()->bounded(256));
    }
    bytes[0] = static_cast<char>((static_cast<quint8>(bytes[0]) & 0x7FU) | 0x01U);
    return bytes;
}

// An X.509 v3 certificate without extensions (RFC 5280 4.1), signed by its own key.
QcResult<QByteArray> certificateDer(const QcTlsKey &key, const QString &commonName)
{
    const QByteArray signatureAlgorithm = QcTlsKey::signatureAlgorithmDer();
    // A day back, for clocks behind ours. It does not expire in practice; Qt cannot read the year
    // 9999 that RFC 5280 suggests for "no well-defined expiration date".
    const QDateTime notBefore = QDateTime::currentDateTimeUtc().addDays(-1);
    const QDateTime notAfter(QDate(9000, 12, 31), QTime(23, 59, 59), QTimeZone::UTC);
    const QByteArray publicKeyInfo = key.subjectPublicKeyInfoDer();
    const QByteArray subject = name(commonName);

    const QByteArray toBeSigned = QcDer::sequence({
        QcDer::explicitTag(0, QcDer::integer(quint8{2})),
        QcDer::integer(serialNumber()),
        signatureAlgorithm,
        subject,
        QcDer::sequence({QcDer::time(notBefore), QcDer::time(notAfter)}),
        subject,
        publicKeyInfo,
    });
    if (publicKeyInfo.isEmpty()) {
        return qcError(u"The server's public key cannot be written"_s);
    }
    const QcResult<QByteArray> signature = key.sign(toBeSigned);
    if (!signature) {
        return qcError(signature.error());
    }
    return QcDer::sequence({toBeSigned, signatureAlgorithm, QcDer::bitString(*signature)});
}

QcResult<QByteArray> readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return qcError(u"Cannot read %1: %2"_s.arg(path, file.errorString()));
    }
    return file.readAll();
}

QcStatus writeFile(const QString &path, const QByteArray &bytes)
{
    if (const QcStatus written = QcSecretFile::write(path, bytes); !written) {
        return qcError(u"Cannot write %1: %2"_s.arg(path, written.error()));
    }
    return {};
}

} // namespace

QcResult<QcTlsCredentials> QcTlsCredentials::generate(const QString &commonName)
{
    const QcResult<QcTlsKey> key = QcTlsKey::generate();
    if (!key) {
        return qcError(key.error());
    }
    const QcResult<QByteArray> certificate = certificateDer(*key, commonName);
    if (!certificate) {
        return qcError(certificate.error());
    }
    return fromPem(QcDer::pem(*certificate, "CERTIFICATE"), key->privateKeyPem());
}

QcResult<QcTlsCredentials> QcTlsCredentials::fromPem(const QByteArray &certificatePem, const QByteArray &keyPem)
{
    // Qt's certificate and key classes belong to the active backend: OpenSSL is chosen first.
    if (const QcStatus tls = QcTlsBackend::ensure(); !tls) {
        return qcError(tls.error());
    }
    QcTlsCredentials credentials{QSslCertificate(certificatePem, QSsl::Pem), QSslKey(keyPem, QcTlsKey::algorithm, QSsl::Pem, QSsl::PrivateKey)};
    if (credentials.certificate.isNull()) {
        return qcError(u"The server certificate cannot be read"_s);
    }
    if (credentials.privateKey.isNull()) {
        return qcError(u"The server key cannot be read"_s);
    }
    if (!QcTlsKey::belongTogether(keyPem, credentials.certificate.toDer())) {
        return qcError(u"The server key does not belong to the server certificate"_s);
    }
    return credentials;
}

QcResult<QcTlsCredentials> QcTlsCredentials::loadOrCreate(const QString &directory)
{
    // Without TLS a server listens without certificate, and none is made.
    if constexpr (!QcTlsBackend::enabled) {
        return QcTlsCredentials();
    }
    const QDir folder(directory);
    const QString certificatePath = folder.filePath(QString::fromLatin1(certificateFileName));
    const QString keyPath = folder.filePath(QString::fromLatin1(keyFileName));
    const bool hasCertificate = QFile::exists(certificatePath);
    const bool hasKey = QFile::exists(keyPath);

    if (hasCertificate && hasKey) {
        const QcResult<QByteArray> certificate = readFile(certificatePath);
        if (!certificate) {
            return qcError(certificate.error());
        }
        const QcResult<QByteArray> key = readFile(keyPath);
        if (!key) {
            return qcError(key.error());
        }
        QcResult<QcTlsCredentials> read = fromPem(*certificate, *key);
        if (!read) {
            return qcError(u"%1 (%2, %3); move both files away to make a new certificate, players then have to forget the old one"_s.arg(
                read.error(), certificatePath, keyPath));
        }
        if (QcSecretFile::othersMayRead(keyPath)) {
            qCWarning(lcConnection).noquote() << u"The server key %1 is readable by other users; limit it to yours (chmod 600)"_s.arg(keyPath);
        }
        return read;
    }
    if (hasCertificate || hasKey) {
        return qcError(u"Only one of %1 and %2 exists; move it away to make a new certificate"_s.arg(certificatePath, keyPath));
    }

    QcResult<QcTlsCredentials> made = generate();
    if (!made) {
        return made;
    }
    QDir().mkpath(folder.absolutePath());
    // The key first: a certificate without its key would block the next start.
    if (const QcStatus written = writeFile(keyPath, made->privateKey.toPem()); !written) {
        return qcError(written.error());
    }
    if (const QcStatus written = writeFile(certificatePath, made->certificate.toPem()); !written) {
        QFile::remove(keyPath);
        return qcError(written.error());
    }
    return made;
}

namespace QcTlsFingerprint {

QByteArray of(const QSslCertificate &certificate)
{
    return certificate.digest(QCryptographicHash::Sha256).toHex();
}

QString display(const QByteArray &hex)
{
    const QString upper = QString::fromLatin1(hex).toUpper();
    QStringList groups;
    for (qsizetype i = 0; i < upper.size(); i += 4) {
        groups.append(upper.mid(i, 4));
    }
    return groups.join(u' ');
}

} // namespace QcTlsFingerprint
