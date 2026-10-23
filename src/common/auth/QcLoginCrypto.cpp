#include "common/auth/QcLoginCrypto.h"

#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>

#include <algorithm>
#include <array>
#include <cstring>

namespace QcLoginCrypto {

namespace {

constexpr QByteArrayView identityLabel = "qtcraft identity v1:";
constexpr QByteArrayView passwordLabel = "qtcraft password v1:";
constexpr QByteArrayView identityProofLabel = "qtcraft identity proof v1:";
constexpr QByteArrayView passwordProofLabel = "qtcraft password proof v1:";

QByteArray joined(QByteArrayView label, QByteArrayView data)
{
    QByteArray bytes;
    bytes.reserve(label.size() + data.size());
    bytes.append(label);
    bytes.append(data);
    return bytes;
}

} // namespace

QByteArray randomBytes(qsizetype count)
{
    QByteArray bytes(count, Qt::Uninitialized);
    std::array<quint32, 8> block{};
    for (qsizetype offset = 0; offset < count; offset += static_cast<qsizetype>(sizeof(block))) {
        QRandomGenerator::system()->fillRange(block.data(), block.size());
        const qsizetype length = std::min<qsizetype>(static_cast<qsizetype>(sizeof(block)), count - offset);
        std::memcpy(bytes.data() + offset, block.data(), static_cast<size_t>(length));
    }
    return bytes;
}

QByteArray hmac(QByteArrayView key, QByteArrayView data)
{
    return QMessageAuthenticationCode::hash(data, key, QCryptographicHash::Sha256);
}

QByteArray pbkdf2(QByteArrayView password, QByteArrayView salt, int rounds)
{
    QMessageAuthenticationCode mac(QCryptographicHash::Sha256, password);
    mac.addData(salt);
    mac.addData(QByteArrayView("\0\0\0\1", 4));
    QByteArray block = mac.result();
    QByteArray result = block;

    for (int round = 1; round < rounds; ++round) {
        mac.reset();
        mac.addData(block);
        block = mac.result();
        for (qsizetype i = 0; i < result.size(); ++i) {
            result[i] = static_cast<char>(result[i] ^ block[i]);
        }
    }
    return result;
}

QByteArray passwordKey(const QString &password, QByteArrayView salt)
{
    if (password.isEmpty()) {
        return {};
    }
    return pbkdf2(password.toUtf8(), joined(passwordLabel, salt), passwordRounds);
}

QByteArray identity(QByteArrayView installationKey, QByteArrayView salt)
{
    return hmac(installationKey, joined(identityLabel, salt));
}

QByteArray proof(QByteArrayView key, QByteArrayView nonce, Purpose purpose)
{
    const QByteArrayView label = purpose == Purpose::Identity ? identityProofLabel : passwordProofLabel;
    return hmac(key, joined(label, nonce));
}

bool equal(QByteArrayView a, QByteArrayView b)
{
    if (a.size() != b.size()) {
        return false;
    }

    unsigned char difference = 0;
    for (qsizetype i = 0; i < a.size(); ++i) {
        difference |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return difference == 0;
}

QString fingerprint(QByteArrayView identity)
{
    const QByteArray hash = QCryptographicHash::hash(identity, QCryptographicHash::Sha256);
    return QString::fromLatin1(hash.first(fingerprintBytes).toHex());
}

} // namespace QcLoginCrypto
