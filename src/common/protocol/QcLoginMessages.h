#pragma once

// Protocol messages of the login of a network client and the list of players (decision E-252).
// Part of the QcMessage variant (QcMessages.h).

#include <QByteArray>
#include <QList>
#include <QString>

// One content pack as server and client compare it: id, version and a hash over its files
// (QcContentFingerprint). An empty hash means "not computed" and only id and version count.
struct QcPackFingerprint
{
    static constexpr qsizetype maxTextLength = 128;

    QString id;
    QString version;
    QString hash;

    friend bool operator==(const QcPackFingerprint &, const QcPackFingerprint &) = default;
};

namespace QcMsg {

// Server -> client after Hello from a remote client: what the server needs for the login. The
// client answers with LoginResponse; local players of the in-process host get Welcome at once.
struct LoginChallenge
{
    static constexpr qsizetype nonceBytes = 32;
    static constexpr qsizetype saltBytes = 16;
    static constexpr qsizetype maxPacks = 256;
    static constexpr qsizetype maxTextLength = 256;

    QString serverName;
    QString motd;
    // Fresh random bytes of this connection: the proofs of the response are bound to them.
    QByteArray nonce;
    // The server's salt for the password key and the player identity (QcLoginCrypto).
    QByteArray salt;
    bool passwordRequired = false;
    // Whether the server knows an identity for the player's name; when not, the client registers
    // one with its response (trust on first use).
    bool identityKnown = false;
    // The largest view distance the server streams, in chunks.
    qint32 maxViewDistance = 0;
    QList<QcPackFingerprint> packs;
};

// Client -> server: the answer to LoginChallenge.
struct LoginResponse
{
    static constexpr qsizetype maxProofBytes = 64;

    // HMAC of the nonce with the password key; empty without a password.
    QByteArray passwordProof;
    // HMAC of the nonce with the player's identity for this server.
    QByteArray identityProof;
    // The identity itself, only when the server does not know one for the name yet.
    QByteArray identity;
    // Chunks the client wants streamed (0: the server's limit).
    qint32 viewDistance = 0;
    QList<QcPackFingerprint> packs;
};

// Server -> client: everyone on the server, sent to all whenever someone joins or leaves, and every
// two seconds while a round trip changed (decision E-886).
struct PlayerList
{
    static constexpr qsizetype maxPlayers = 1024;

    struct Entry
    {
        quint32 entityId = 0;
        QString name;
        bool operatorRights = false;
        // In spectator mode (decision E-730): listed, but shown apart.
        bool spectator = false;
        // The round trip the player's client measured (decision E-886), smoothed, in microseconds;
        // -1 while unknown.
        qint32 latencyUs = -1;

        friend bool operator==(const Entry &, const Entry &) = default;
    };
    QList<Entry> players;
};

} // namespace QcMsg
