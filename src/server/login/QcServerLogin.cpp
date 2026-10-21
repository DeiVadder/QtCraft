#include "server/login/QcServerLogin.h"

#include "common/auth/QcLoginCrypto.h"
#include "common/auth/QcProfileValidation.h"
#include "common/core/QcNameText.h"
#include "common/protocol/QcChatText.h"
#include "common/protocol/QcPackComparison.h"
#include "common/protocol/QcProtocol.h"
#include "server/core/QcServerLogging.h"
#include "server/core/QcServerState.h"


#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

using Purpose = QcLoginCrypto::Purpose;

} // namespace

QcServerLogin::QcServerLogin(QcServerState &state, Join join)
    : m_state(state)
    , m_join(std::move(join))
{
}

void QcServerLogin::refuse(QcServerClient &client, const QcMsg::Disconnect &reason)
{
    qCInfo(lcServer).noquote() << u"Player %1 (%2) refused: %3"_s.arg(client.name, client.login.address, reason.reason);
    client.connection->send(reason);
    client.connection->close();
    client.state = QcServerClient::State::Closed;
}

void QcServerLogin::hello(QcServerClient &client, const QcMsg::Hello &hello)
{
    if (client.state != QcServerClient::State::AwaitingHello) {
        return;
    }

    client.name = QcProfileValidation::playerName(hello.playerName);
    // The text comes from the other side: no line breaks, control or invisible format characters
    // (direction overrides) in the log.
    QString text = hello.client;
    std::ranges::replace_if(
        text,
        [](QChar c) {
            const QChar::Category category = c.category();
            return category == QChar::Other_Control || category == QChar::Other_Format || category == QChar::Separator_Line
                   || category == QChar::Separator_Paragraph;
        },
        u'?');
    client.login.client = text.isEmpty() ? u"unknown client"_s : text;
    if (!client.local) {
        qCInfo(lcServer).noquote() << u"Hello from %1: player %2, protocol %3, %4"_s.arg(client.login.address, client.name)
                                          .arg(hello.protocolVersion)
                                          .arg(client.login.client);
    }
    if (hello.protocolVersion != QcProtocol::version) {
        refuse(client, QcChatText::disconnect(QT_TRANSLATE_NOOP("QcServerMessages", "Protocol version mismatch: server %1, client %2"),
                                              {QString::number(QcProtocol::version), QString::number(hello.protocolVersion)}));
        return;
    }
    // Other clients show names as plain text; markup characters are refused all the same.
    if (QcProfileValidation::hasMarkup(QcNameText::clean(hello.playerName, QcProfileValidation::maxPlayerNameLength))) {
        refuse(client, QcChatText::disconnect(QT_TRANSLATE_NOOP("QcServerMessages", "Player names may not contain <, > or &")));
        return;
    }

    client.id = m_state.entities().newId();
    client.appearance = QcProfileValidation::appearance(m_state.content(), hello.appearance);
    client.view.distance = m_state.config().viewDistance;
    if (client.local) {
        // The player who opened the world: no login, always admitted.
        m_join(client);
        return;
    }

    client.state = QcServerClient::State::AwaitingLogin;
    client.login.nonce = QcLoginCrypto::randomBytes(QcMsg::LoginChallenge::nonceBytes);
    client.connection->send(QcMsg::LoginChallenge{m_state.config().serverName.left(QcMsg::LoginChallenge::maxTextLength),
                                                  m_state.config().motd.left(QcMsg::LoginChallenge::maxTextLength),
                                                  client.login.nonce,
                                                  m_state.identities().salt(),
                                                  !m_state.config().password.isEmpty(),
                                                  m_state.identities().find(client.name) != nullptr,
                                                  std::clamp(m_state.config().viewDistance, 1, QcMsg::Welcome::maxViewDistance),
                                                  serverPacks()});
}

void QcServerLogin::response(QcServerClient &client, const QcMsg::LoginResponse &response)
{
    if (client.state != QcServerClient::State::AwaitingLogin) {
        refuse(client, QcChatText::disconnect(QT_TRANSLATE_NOOP("QcServerMessages", "Unexpected login message")));
        return;
    }

    const std::expected<QByteArray, QcMsg::Disconnect> newIdentity = verify(client, response);
    const std::optional<QcMsg::Disconnect> refusal = newIdentity ? admission(client) : std::optional(newIdentity.error());
    if (refusal) {
        refuse(client, *refusal);
        return;
    }

    if (response.viewDistance > 0) {
        client.view.distance = std::min(response.viewDistance, m_state.config().viewDistance);
    }
    admit(client, *newIdentity);
}

std::expected<QByteArray, QcMsg::Disconnect> QcServerLogin::verify(QcServerClient &client, const QcMsg::LoginResponse &response)
{
    if (!m_state.config().password.isEmpty()
        && !QcLoginCrypto::equal(response.passwordProof, QcLoginCrypto::proof(passwordKey(), client.login.nonce, Purpose::Password))) {
        return std::unexpected(QcChatText::disconnect(QT_TRANSLATE_NOOP("QcServerMessages", "Wrong server password")));
    }

    const QStringList differences = QcPackComparison::differences(serverPacks(), response.packs);
    if (!differences.isEmpty()) {
        return std::unexpected(QcChatText::disconnect(QT_TRANSLATE_NOOP("QcServerMessages", "Your game content differs from the server's: %1"),
                                                      {QcPackComparison::summary(differences)}));
    }
    return verifyIdentity(client, response);
}

std::expected<QByteArray, QcMsg::Disconnect> QcServerLogin::verifyIdentity(QcServerClient &client, const QcMsg::LoginResponse &response) const
{
    const QcIdentityStore::Identity *known = m_state.identities().find(client.name);
    const QByteArray &identity = known ? known->identity : response.identity;
    if (!known && identity.size() != QcLoginCrypto::keyBytes) {
        return std::unexpected(QcChatText::disconnect(QT_TRANSLATE_NOOP("QcServerMessages", "Invalid player identity")));
    }

    // A proof with the registered identity shows the client has the key behind the name.
    if (!QcLoginCrypto::equal(response.identityProof, QcLoginCrypto::proof(identity, client.login.nonce, Purpose::Identity))) {
        return std::unexpected(known ? QcChatText::disconnect(QT_TRANSLATE_NOOP("QcServerMessages", "The name %1 belongs to another player on this server"),
                                                              {client.name})
                                     : QcChatText::disconnect(QT_TRANSLATE_NOOP("QcServerMessages", "Invalid player identity")));
    }

    // A full store would let newcomers in without binding their name to them.
    if (!known && m_state.identities().isFull()) {
        return std::unexpected(QcChatText::disconnect(QT_TRANSLATE_NOOP("QcServerMessages", "This server cannot register new players")));
    }

    client.login.fingerprint = QcLoginCrypto::fingerprint(identity);
    return known ? QByteArray() : identity;
}

std::optional<QcMsg::Disconnect> QcServerLogin::admission(const QcServerClient &client) const
{
    if (std::optional<QcMsg::Disconnect> refusal = m_state.access().refusal(client.name, client.login.fingerprint, client.local)) {
        return refusal;
    }

    const QString nameKey = QcAccessList::key(client.name);
    int players = 0;
    bool nameTaken = false;
    for (const auto &other : m_state.clients()) {
        if (other.get() != &client && other->state == QcServerClient::State::Playing) {
            ++players;
            nameTaken = nameTaken || QcAccessList::key(other->name) == nameKey;
        }
    }

    if (nameTaken) {
        return QcChatText::disconnect(QT_TRANSLATE_NOOP("QcServerMessages", "A player named %1 is already on this server"), {client.name});
    }
    // Operators come in even when the server is full, so they can make room.
    if (players >= m_state.config().maxPlayers && !m_state.access().isOperator(client.name, client.login.fingerprint)) {
        return QcChatText::disconnect(QT_TRANSLATE_NOOP("QcServerMessages", "The server is full (%1 players)"),
                                      {QString::number(m_state.config().maxPlayers)});
    }
    return std::nullopt;
}

void QcServerLogin::admit(QcServerClient &client, const QByteArray &newIdentity)
{
    // Registered only once everything else passed, so a refused client claims no name.
    if (!newIdentity.isEmpty()) {
        if (!m_state.identities().add(client.name, newIdentity, QDateTime::currentDateTimeUtc())) {
            refuse(client, QcChatText::disconnect(QT_TRANSLATE_NOOP("QcServerMessages", "This server cannot register new players")));
            return;
        }
        qCInfo(lcServer).noquote() << u"Player %1 registered the identity %2"_s.arg(client.name, client.login.fingerprint);
    }
    if (const QcResult<bool> bound = m_state.access().bind(client.name, client.login.fingerprint); !bound) {
        qCWarning(lcServer).noquote() << bound.error();
    }

    client.login.nonce.clear();
    qCInfo(lcServer).noquote() << u"Player %1 logged in from %2 (identity %3, %4)"_s.arg(client.name, client.login.address, client.login.fingerprint,
                                                                                          client.login.client);
    m_join(client);
}

void QcServerLogin::expire()
{
    for (const auto &client : m_state.clients()) {
        // The local player of the in-process host has no login to wait for (a slow start of the
        // game must not shut its owner out).
        const bool waiting = client->state == QcServerClient::State::AwaitingHello || client->state == QcServerClient::State::AwaitingLogin;
        if (waiting && !client->local && client->login.connected.isValid() && client->login.connected.hasExpired(m_state.config().loginTimeoutMs)) {
            refuse(*client, QcChatText::disconnect(QT_TRANSLATE_NOOP("QcServerMessages", "The login took too long")));
        }
    }
}

QList<QcPackFingerprint> QcServerLogin::serverPacks() const
{
    if (!m_state.config().contentPacks.isEmpty()) {
        return m_state.config().contentPacks;
    }

    QList<QcPackFingerprint> packs;
    for (const QcPackReference &pack : m_state.content().packs()) {
        packs.append({pack.id, pack.version, {}});
    }
    return packs;
}

const QByteArray &QcServerLogin::passwordKey()
{
    if (!m_passwordKeyReady) {
        m_passwordKey = QcLoginCrypto::passwordKey(m_state.config().password, m_state.identities().salt());
        m_passwordKeyReady = true;
    }
    return m_passwordKey;
}
