#include "network/tls/QcTlsKey.h"

#include "network/tls/QcDer.h"
#include "network/tls/QcTlsBackend.h"

#include <QCoreApplication>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

namespace {

// ecdsa-with-SHA256, 1.2.840.10045.4.3.2.
constexpr char ecdsaWithSha256[] = "\x2A\x86\x48\xCE\x3D\x04\x03\x02";

// The reason of OpenSSL's latest error on this thread, which is taken off its error queue.
QString lastError()
{
    const unsigned long code = ERR_get_error();
    ERR_clear_error();
    if (code == 0) {
        return QStringLiteral("unknown error");
    }
    char text[256] = {};
    ERR_error_string_n(code, text, sizeof(text));
    return QString::fromLatin1(text);
}

const unsigned char *bytes(const QByteArray &data)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): OpenSSL takes bytes as unsigned char, QByteArray holds char
    return reinterpret_cast<const unsigned char *>(data.constData());
}

std::shared_ptr<EVP_MD_CTX> digestContext()
{
    return {EVP_MD_CTX_new(), EVP_MD_CTX_free};
}

} // namespace

QcResult<QcTlsKey> QcTlsKey::generate()
{
    // The backend first: it initialises OpenSSL without reading any configuration file.
    if (const QcStatus tls = QcTlsBackend::ensure(); !tls) {
        return qcError(tls.error());
    }
    QcTlsKey key;
    key.m_key = std::shared_ptr<EVP_PKEY>(EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-256"), EVP_PKEY_free);
    if (!key.m_key) {
        return qcError(QCoreApplication::translate("QcNetwork", "The server's key cannot be made: %1").arg(lastError()));
    }
    return key;
}

QByteArray QcTlsKey::subjectPublicKeyInfoDer() const
{
    const int length = i2d_PUBKEY(m_key.get(), nullptr);
    if (length <= 0) {
        return {};
    }
    QByteArray der(length, '\0');
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): OpenSSL takes bytes as unsigned char, QByteArray holds char
    auto *cursor = reinterpret_cast<unsigned char *>(der.data());
    return i2d_PUBKEY(m_key.get(), &cursor) == length ? der : QByteArray();
}

QByteArray QcTlsKey::signatureAlgorithmDer()
{
    // Without parameters (RFC 5758, 3.2), unlike the NULL of the RSA identifiers.
    return QcDer::sequence({QcDer::objectIdentifier(QByteArray(ecdsaWithSha256))});
}

QByteArray QcTlsKey::privateKeyPem() const
{
    const std::shared_ptr<BIO> memory(BIO_new(BIO_s_mem()), BIO_free);
    if (!memory || PEM_write_bio_PrivateKey(memory.get(), m_key.get(), nullptr, nullptr, 0, nullptr, nullptr) != 1) {
        return {};
    }
    char *data = nullptr;
    const long length = BIO_get_mem_data(memory.get(), &data);
    return length > 0 ? QByteArray(data, static_cast<qsizetype>(length)) : QByteArray();
}

QcResult<QByteArray> QcTlsKey::sign(const QByteArray &message) const
{
    const std::shared_ptr<EVP_MD_CTX> context = digestContext();
    std::size_t length = 0;
    const auto size = static_cast<std::size_t>(message.size());
    if (!context || EVP_DigestSignInit(context.get(), nullptr, EVP_sha256(), nullptr, m_key.get()) != 1 ||
        EVP_DigestSign(context.get(), nullptr, &length, bytes(message), size) != 1) {
        return qcError(QCoreApplication::translate("QcNetwork", "The server's certificate cannot be signed: %1").arg(lastError()));
    }

    // The first call gives the largest length; an ECDSA signature in DER may come out shorter.
    QByteArray signature(static_cast<qsizetype>(length), '\0');
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): OpenSSL takes bytes as unsigned char, QByteArray holds char
    auto *output = reinterpret_cast<unsigned char *>(signature.data());
    if (EVP_DigestSign(context.get(), output, &length, bytes(message), size) != 1) {
        return qcError(QCoreApplication::translate("QcNetwork", "The server's certificate cannot be signed: %1").arg(lastError()));
    }
    signature.truncate(static_cast<qsizetype>(length));
    return signature;
}

bool QcTlsKey::belongTogether(const QByteArray &keyPem, const QByteArray &certificateDer)
{
    const std::shared_ptr<BIO> memory(BIO_new_mem_buf(keyPem.constData(), static_cast<int>(keyPem.size())), BIO_free);
    const std::shared_ptr<EVP_PKEY> key(memory ? PEM_read_bio_PrivateKey(memory.get(), nullptr, nullptr, nullptr) : nullptr, EVP_PKEY_free);
    const unsigned char *cursor = bytes(certificateDer);
    const std::shared_ptr<X509> certificate(d2i_X509(nullptr, &cursor, static_cast<long>(certificateDer.size())), X509_free);
    const bool together = key && certificate && X509_check_private_key(certificate.get(), key.get()) == 1;
    ERR_clear_error();
    return together;
}

bool QcTlsKey::verify(const QByteArray &message, const QByteArray &signature) const
{
    const std::shared_ptr<EVP_MD_CTX> context = digestContext();
    const bool valid = context && EVP_DigestVerifyInit(context.get(), nullptr, EVP_sha256(), nullptr, m_key.get()) == 1 &&
                       EVP_DigestVerify(context.get(), bytes(signature), static_cast<std::size_t>(signature.size()), bytes(message),
                                        static_cast<std::size_t>(message.size())) == 1;
    ERR_clear_error();
    return valid;
}
