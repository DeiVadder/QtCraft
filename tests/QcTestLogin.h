#pragma once

#include "common/auth/QcLoginCrypto.h"
#include "common/protocol/QcLoginMessages.h"
#include "common/protocol/QcProtocol.h"

#include <QCryptographicHash>

#include <optional>

// The login of the test clients (decision E-252), answered like the real client does it.
namespace QcTestLogin {

// The installation key of a test player: made from the name without case, so the same name always
// has the same identity, also when typed in other case.
inline QByteArray keyFor(const QString &name)
{
    return QCryptographicHash::hash(QByteArray(QByteArray("raw test key:") + name.toCaseFolded().toUtf8()), QCryptographicHash::Sha256);
}

struct Credentials
{
    QByteArray key;
    QString password;
    // None: the server's own packs.
    std::optional<QList<QcPackFingerprint>> packs;
    qint32 viewDistance = 0;
};

inline QcMsg::LoginResponse answer(const QcMsg::LoginChallenge &challenge, const Credentials &credentials)
{
    using QcLoginCrypto::Purpose;
    QcMsg::LoginResponse response;
    const QByteArray identity = QcLoginCrypto::identity(credentials.key, challenge.salt);
    response.identityProof = QcLoginCrypto::proof(identity, challenge.nonce, Purpose::Identity);
    response.identity = challenge.identityKnown ? QByteArray() : identity;
    if (!credentials.password.isEmpty()) {
        const QByteArray key = QcLoginCrypto::passwordKey(credentials.password, challenge.salt);
        response.passwordProof = QcLoginCrypto::proof(key, challenge.nonce, Purpose::Password);
    }
    response.packs = credentials.packs.value_or(challenge.packs);
    response.viewDistance = credentials.viewDistance;
    return response;
}

} // namespace QcTestLogin
