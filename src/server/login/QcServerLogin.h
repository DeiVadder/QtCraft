#pragma once

#include "common/protocol/QcLoginMessages.h"
#include "common/protocol/QcSessionMessages.h"

#include <QByteArray>

#include <expected>
#include <functional>
#include <optional>

struct QcServerClient;
class QcServerState;

// The server's side of the handshake (decision E-252). Hello checks the protocol version and the
// name; a local player (in-process host) joins at once, a remote client gets a LoginChallenge and
// must answer it within the login timeout: the server password, the same content packs and the
// identity registered for its name (or a new one: trust on first use). Then bans and whitelist (by
// name and fingerprint, decision E-253), the player limit and names already on the server decide.
// Every refusal is a translatable Disconnect.
class QcServerLogin
{
public:
    // Makes the admitted client a player (restores or spawns him, sends Welcome).
    using Join = std::function<void(QcServerClient &client)>;

    QcServerLogin(QcServerState &state, Join join);

    void hello(QcServerClient &client, const QcMsg::Hello &hello);
    void response(QcServerClient &client, const QcMsg::LoginResponse &response);
    // Disconnects clients whose login is not finished after the configured time.
    void expire();
    static void refuse(QcServerClient &client, const QcMsg::Disconnect &reason);

    // The password changed (a world opened to the network): the derived key is made anew.
    void forgetPasswordKey() { m_passwordKeyReady = false; }
    // The packs a client must have: the configured fingerprints, else id and version of the content.
    QList<QcPackFingerprint> serverPacks() const;

private:
    // Password, content and identity; sets the client's fingerprint. The identity the name registers
    // (empty when it has one already), or why the client is refused.
    [[nodiscard]] std::expected<QByteArray, QcMsg::Disconnect> verify(QcServerClient &client, const QcMsg::LoginResponse &response);
    [[nodiscard]] std::expected<QByteArray, QcMsg::Disconnect> verifyIdentity(QcServerClient &client, const QcMsg::LoginResponse &response) const;
    // Bans, whitelist, the player limit and a player of the same name on the server.
    [[nodiscard]] std::optional<QcMsg::Disconnect> admission(const QcServerClient &client) const;
    void admit(QcServerClient &client, const QByteArray &newIdentity);
    // Derived once from the configured password and the world's salt (PBKDF2 costs time).
    const QByteArray &passwordKey();

    QcServerState &m_state;
    Join m_join;
    QByteArray m_passwordKey;
    bool m_passwordKeyReady = false;
};
