#include "clientcore/session/QcClientLogin.h"

#include "common/auth/QcLoginCrypto.h"

#include <algorithm>

void QcClientLogin::setCredentials(Credentials credentials)
{
    m_credentials = std::move(credentials);
}

QcMsg::LoginResponse QcClientLogin::answer(const QcMsg::LoginChallenge &challenge)
{
    using Purpose = QcLoginCrypto::Purpose;

    m_serverName = challenge.serverName;
    m_motd = challenge.motd;
    m_passwordMissing = challenge.passwordRequired && m_credentials.password.isEmpty();
    if (m_credentials.installationKey.size() != QcLoginCrypto::keyBytes) {
        m_credentials.installationKey = QcLoginCrypto::randomBytes(QcLoginCrypto::keyBytes);
    }

    QcMsg::LoginResponse response;
    const QByteArray identity = QcLoginCrypto::identity(m_credentials.installationKey, challenge.salt);
    response.identityProof = QcLoginCrypto::proof(identity, challenge.nonce, Purpose::Identity);
    if (!challenge.identityKnown) {
        response.identity = identity;
    }
    if (challenge.passwordRequired && !m_credentials.password.isEmpty()) {
        const QByteArray key = QcLoginCrypto::passwordKey(m_credentials.password, challenge.salt);
        response.passwordProof = QcLoginCrypto::proof(key, challenge.nonce, Purpose::Password);
    }

    response.viewDistance = std::clamp(m_credentials.viewDistance, 0, int(challenge.maxViewDistance));
    response.packs = m_credentials.packs;
    return response;
}
