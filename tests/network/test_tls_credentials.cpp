#include "network/tls/QcDer.h"
#include "network/tls/QcTlsBackend.h"
#include "network/tls/QcTlsCredentials.h"
#include "network/tls/QcTlsKey.h"
#include "network/tls/QcTlsListener.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QSslCipher>
#include <QSslSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <openssl/opensslv.h>

#ifdef Q_OS_MACOS
#include <mach-o/dyld.h>
#endif

using namespace Qt::StringLiterals;

namespace {

constexpr int timeoutMs = 20'000;

} // namespace

// The server's own TLS key and certificate (decision E-872): the DER encoding, OpenSSL's key
// and its signature, the certificate as Qt reads it, a handshake with it on the loopback interface,
// and keeping the files in the data folder.
class TestTlsCredentials : public QObject
{
    Q_OBJECT

private:
    // One key for the whole test: making one is the slow part.
    std::optional<QcTlsCredentials> m_credentials;
    // Holds an OpenSSL configuration that the game must not read (initTestCase).
    QTemporaryDir m_configuration;

    const QcTlsCredentials &credentials()
    {
        if (!m_credentials) {
            QElapsedTimer timer;
            timer.start();
            QcResult<QcTlsCredentials> made = QcTlsCredentials::generate();
            qInfo().noquote() << u"Making the key and certificate took %1 ms"_s.arg(timer.elapsed());
            m_credentials = made ? std::move(*made) : QcTlsCredentials();
        }
        return *m_credentials;
    }

private slots:
    // Before OpenSSL starts: a configuration through OPENSSL_CONF that would allow only ChaCha20
    // in TLS 1.3. The game initialises OpenSSL without reading it, so the handshake below still
    // picks AES-256-GCM.
    void initTestCase()
    {
        QVERIFY(m_configuration.isValid());
        const QString path = m_configuration.filePath(u"openssl.cnf"_s);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("openssl_conf = qc_init\n[qc_init]\nssl_conf = qc_ssl\n[qc_ssl]\nsystem_default = qc_system\n"
                   "[qc_system]\nCiphersuites = TLS_CHACHA20_POLY1305_SHA256\n");
        file.close();
        qputenv("OPENSSL_CONF", QFile::encodeName(path));
    }

    // The DER the certificate is written in: integers stay positive and minimal, long lengths.
    void derEncoding()
    {
        QCOMPARE(QcDer::integer(quint8{2}), QByteArray::fromHex("020102"));
        QCOMPARE(QcDer::integer(QByteArray::fromHex("0000ff")), QByteArray::fromHex("020200ff"));
        QCOMPARE(QcDer::integer(QByteArray()), QByteArray::fromHex("020100"));
        QCOMPARE(QcDer::element(0x04, QByteArray(200, 'x')).left(3), QByteArray::fromHex("0481c8"));
        QCOMPARE(QcDer::element(0x04, QByteArray(300, 'x')).left(4), QByteArray::fromHex("0482012c"));
        QCOMPARE(QcDer::time(QDateTime(QDate(2026, 10, 23), QTime(12, 0), QTimeZone::UTC)), QByteArray("\x17\x0d" "261023120000Z"));
        QCOMPARE(QcDer::time(QDateTime(QDate(9000, 12, 31), QTime(23, 59, 59), QTimeZone::UTC)), QByteArray("\x18\x0f" "90001231235959Z"));
    }

    // Qt's OpenSSL backend is active and runs on the OpenSSL the game ships, not one of the system.
    void bundledOpenSsl()
    {
        const QcStatus tls = QcTlsBackend::ensure();
        QVERIFY2(tls, qPrintable(tls.error_or(QString())));
        QCOMPARE(QSslSocket::activeBackend(), u"openssl"_s);
        QVERIFY(QSslSocket::supportsSsl());
        QVERIFY2(QcTlsBackend::libraryVersion().startsWith(u"OpenSSL %1 "_s.arg(QStringLiteral(OPENSSL_FULL_VERSION_STR))),
                 qPrintable(QcTlsBackend::libraryVersion()));
        QCOMPARE(QSslSocket::sslLibraryVersionNumber(), OPENSSL_VERSION_NUMBER);
#ifdef Q_OS_MACOS
        // The one libssl in the process is the one of the build (its rpath), not one of Homebrew.
        QStringList loaded;
        for (uint32_t i = 0; i < _dyld_image_count(); ++i) {
            const QString image = QString::fromUtf8(_dyld_get_image_name(i));
            if (image.endsWith(u"/libssl.3.dylib"_s) || image.endsWith(u"/libcrypto.3.dylib"_s)) {
                loaded.append(QFileInfo(image).canonicalFilePath());
            }
        }
        loaded.sort();
        const QString prefix = QFileInfo(QStringLiteral(QC_TEST_OPENSSL_PREFIX "/lib")).canonicalFilePath();
        QCOMPARE(loaded, (QStringList{prefix + u"/libcrypto.3.dylib"_s, prefix + u"/libssl.3.dylib"_s}));
#endif
    }

    // OpenSSL's key signs, and the signature holds only for its message.
    void keySignsAndVerifies()
    {
        const QcResult<QcTlsKey> key = QcTlsKey::generate();
        QVERIFY2(key, qPrintable(key.error_or(QString())));
        // SubjectPublicKeyInfo of an uncompressed P-256 point: 91 bytes.
        QCOMPARE(key->subjectPublicKeyInfoDer().size(), 91);
        QVERIFY(key->privateKeyPem().startsWith("-----BEGIN PRIVATE KEY-----\n"));
        const QcResult<QByteArray> signature = key->sign("QtCraft");
        QVERIFY(signature);
        // An Ecdsa-Sig-Value: a SEQUENCE of two INTEGERs of up to 33 bytes each.
        QVERIFY(signature->startsWith('\x30'));
        QVERIFY(signature->size() >= 8 && signature->size() <= 72);
        QVERIFY(key->verify("QtCraft", *signature));
        QVERIFY(!key->verify("QtCrafT", *signature));
        QByteArray broken = *signature;
        broken[10] = static_cast<char>(broken[10] ^ 1);
        QVERIFY(!key->verify("QtCraft", broken));
    }

    // Qt reads the certificate: subject, issuer, validity, public key and a correct self-signature.
    void certificateAsQtReadsIt()
    {
        const QcTlsCredentials &made = credentials();
        QVERIFY(!made.isNull());
        QCOMPARE(made.certificate.subjectInfo(QSslCertificate::CommonName), QStringList{u"QtCraft server"_s});
        QCOMPARE(made.certificate.issuerInfo(QSslCertificate::CommonName), QStringList{u"QtCraft server"_s});
        QVERIFY(made.certificate.isSelfSigned());
        QCOMPARE(made.certificate.version(), QByteArray("3"));
        QCOMPARE(made.certificate.expiryDate().date(), QDate(9000, 12, 31));
        QVERIFY(made.certificate.effectiveDate() < QDateTime::currentDateTimeUtc());
        QCOMPARE(made.certificate.publicKey().algorithm(), QSsl::Ec);
        QCOMPARE(made.certificate.publicKey().length(), 256);
        QCOMPARE(made.privateKey.algorithm(), QSsl::Ec);
        QCOMPARE(made.privateKey.length(), 256);
        QCOMPARE(QcTlsFingerprint::of(made.certificate).size(), 64);
        QCOMPARE(QcTlsFingerprint::display("0123456789abcdef"), u"0123 4567 89AB CDEF"_s);
    }

    // A TLS client on the loopback interface finishes the handshake with the listener and sees the
    // certificate the client pins.
    void handshake()
    {
        const QcTlsCredentials &made = credentials();
        QVERIFY(!made.isNull());
        QcTlsListener listener(made);
        QVERIFY(listener.listen(QHostAddress::LocalHost, 0));

        QSslSocket client;
        QSslConfiguration configuration = QSslConfiguration::defaultConfiguration();
        configuration.setPeerVerifyMode(QSslSocket::QueryPeer);
        client.setSslConfiguration(configuration);
        QObject::connect(&client, &QSslSocket::sslErrors, &client, [&client] { client.ignoreSslErrors(); });
        QSignalSpy encrypted(&client, &QSslSocket::encrypted);
        client.connectToHostEncrypted(u"127.0.0.1"_s, listener.serverPort());
        QTRY_VERIFY_WITH_TIMEOUT(listener.hasPendingConnections(), timeoutMs);
        QTcpSocket *accepted = listener.nextPendingConnection();
        QVERIFY(qobject_cast<QSslSocket *>(accepted));
        QcTlsListener::startEncryption(accepted);

        QTRY_COMPARE_WITH_TIMEOUT(encrypted.count(), 1, timeoutMs);
        QCOMPARE(QcTlsFingerprint::of(client.peerCertificate()), QcTlsFingerprint::of(made.certificate));
        QCOMPARE(client.sessionProtocol(), QSsl::TlsV1_3);
        // Not the ChaCha20 of the configuration in OPENSSL_CONF (initTestCase).
        QCOMPARE(client.sessionCipher().name(), u"TLS_AES_256_GCM_SHA384"_s);

        client.write("hello");
        QTRY_COMPARE_WITH_TIMEOUT(accepted->bytesAvailable(), 5, timeoutMs);
        QCOMPARE(accepted->readAll(), QByteArray("hello"));
    }

    // The listener speaks TLS 1.3 only: a client that offers at most TLS 1.2 gets no connection.
    void olderProtocolsAreRefused()
    {
        const QcTlsCredentials &made = credentials();
        QVERIFY(!made.isNull());
        QcTlsListener listener(made);
        QVERIFY(listener.listen(QHostAddress::LocalHost, 0));

        QSslSocket client;
        QSslConfiguration configuration = QSslConfiguration::defaultConfiguration();
        configuration.setProtocol(QSsl::TlsV1_2);
        configuration.setPeerVerifyMode(QSslSocket::QueryPeer);
        client.setSslConfiguration(configuration);
        QObject::connect(&client, &QSslSocket::sslErrors, &client, [&client] { client.ignoreSslErrors(); });
        QSignalSpy encrypted(&client, &QSslSocket::encrypted);
        QSignalSpy errors(&client, &QSslSocket::errorOccurred);
        client.connectToHostEncrypted(u"127.0.0.1"_s, listener.serverPort());
        QTRY_VERIFY_WITH_TIMEOUT(listener.hasPendingConnections(), timeoutMs);
        QcTlsListener::startEncryption(listener.nextPendingConnection());
        QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(), timeoutMs);
        QCOMPARE(encrypted.count(), 0);
    }

    // A key and a certificate that do not belong together are an error, not a server that fails
    // every handshake.
    void aKeyOfAnotherCertificateIsRefused()
    {
        const QcTlsCredentials &made = credentials();
        QVERIFY(!made.isNull());
        const QcResult<QcTlsKey> other = QcTlsKey::generate();
        QVERIFY(other);
        QVERIFY(QcTlsCredentials::fromPem(made.certificate.toPem(), made.privateKey.toPem()));
        const QcResult<QcTlsCredentials> mixed = QcTlsCredentials::fromPem(made.certificate.toPem(), other->privateKeyPem());
        QVERIFY(!mixed);
        QVERIFY2(mixed.error().contains(u"does not belong"_s), qPrintable(mixed.error()));
    }

    // The files are made once and read again unchanged; a damaged or lone file is never replaced.
    void filesInTheDataFolder()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QcResult<QcTlsCredentials> first = QcTlsCredentials::loadOrCreate(directory.path());
        QVERIFY2(first, qPrintable(first.error_or(QString())));
        const QcResult<QcTlsCredentials> second = QcTlsCredentials::loadOrCreate(directory.path());
        QVERIFY2(second, qPrintable(second.error_or(QString())));
        QCOMPARE(QcTlsFingerprint::of(second->certificate), QcTlsFingerprint::of(first->certificate));
        QCOMPARE(second->privateKey.toPem(), first->privateKey.toPem());

        const QString keyPath = directory.filePath(QString::fromLatin1(QcTlsCredentials::keyFileName));
        QCOMPARE(QFile::permissions(keyPath) & (QFileDevice::ReadGroup | QFileDevice::ReadOther), QFileDevice::Permissions{});
        // A key readable by others still loads, with a warning.
        QVERIFY(QFile::setPermissions(keyPath, QFile::permissions(keyPath) | QFileDevice::ReadGroup | QFileDevice::ReadOther));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"server key .* is readable by other users"_s));
        QVERIFY(QcTlsCredentials::loadOrCreate(directory.path()));

        QFile key(keyPath);
        QVERIFY(key.open(QIODevice::WriteOnly | QIODevice::Truncate));
        key.write("broken");
        key.close();
        const QcResult<QcTlsCredentials> damaged = QcTlsCredentials::loadOrCreate(directory.path());
        QVERIFY(!damaged);
        QVERIFY2(damaged.error().contains(u"move both files away"_s), qPrintable(damaged.error()));
        QCOMPARE(QFile(keyPath).size(), 6);

        QVERIFY(QFile::remove(keyPath));
        const QcResult<QcTlsCredentials> lone = QcTlsCredentials::loadOrCreate(directory.path());
        QVERIFY(!lone);
        QVERIFY2(lone.error().contains(u"Only one of"_s), qPrintable(lone.error()));
        QVERIFY(!QFile::exists(keyPath));
    }
};

QTEST_GUILESS_MAIN(TestTlsCredentials)
#include "test_tls_credentials.moc"
