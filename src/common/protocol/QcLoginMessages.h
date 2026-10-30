// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Protocol messages of the login of a network client and the list of players (decision E-252).
// Part of the QcMessage variant (QcMessages.h).

#include <QByteArray>
#include <QList>
#include <QString>

/*! \brief One content pack as server and client compare it: id, version and a hash over its files
    (QcContentFingerprint).

    An empty hash means "not computed" and only id and version count. */
struct QcPackFingerprint
{
    /*! The longest id, version and hash; the decoder refuses longer ones. */
    static constexpr qsizetype maxTextLength = 128;

    /*! The pack's id. */
    QString id;
    /*! The pack's version. */
    QString version;
    /*! The hash over its files; empty when not computed. */
    QString hash;

    friend bool operator==(const QcPackFingerprint &, const QcPackFingerprint &) = default;
};

/* QcMsg is documented in QcSessionMessages.h. */
namespace QcMsg {

/*! \brief Server -> client after Hello from a remote client: what the server needs for the login.

    The client answers with LoginResponse; local players of the in-process host get Welcome at
    once. */
struct LoginChallenge
{
    /*! The size of the nonce and of the salt; the decoder refuses other sizes. \qcshared */
    static constexpr qsizetype nonceBytes = 32;
    static constexpr qsizetype saltBytes = 16;
    /*! The most packs of a challenge or response; the decoder refuses more. */
    static constexpr qsizetype maxPacks = 256;
    /*! The longest server name and message of the day. */
    static constexpr qsizetype maxTextLength = 256;

    /*! The server's name. */
    QString serverName;
    /*! The server's message of the day. */
    QString motd;
    /*! Fresh random bytes of this connection: the proofs of the response are bound to them. */
    QByteArray nonce;
    /*! The server's salt for the password key and the player identity (QcLoginCrypto). */
    QByteArray salt;
    /*! Whether the server asks for a password. */
    bool passwordRequired = false;
    /*! Whether the server knows an identity for the player's name; when not, the client
        registers one with its response (trust on first use). */
    bool identityKnown = false;
    /*! The largest view distance the server streams, in chunks. */
    qint32 maxViewDistance = 0;
    /*! The server's content packs. */
    QList<QcPackFingerprint> packs;
};

/*! \brief Client -> server: the answer to LoginChallenge. */
struct LoginResponse
{
    /*! The longest proof or identity; the decoder refuses longer ones. */
    static constexpr qsizetype maxProofBytes = 64;

    /*! HMAC of the nonce with the password key; empty without a password. */
    QByteArray passwordProof;
    /*! HMAC of the nonce with the player's identity for this server. */
    QByteArray identityProof;
    /*! The identity itself, only when the server does not know one for the name yet. */
    QByteArray identity;
    /*! Chunks the client wants streamed (0: the server's limit). */
    qint32 viewDistance = 0;
    /*! The client's content packs, which the server compares with its own. */
    QList<QcPackFingerprint> packs;
};

/*! \brief Server -> client: everyone on the server, sent to all whenever someone joins or
    leaves, and every two seconds while a round trip changed (decision E-886). */
struct PlayerList
{
    /*! The most players of a list; the decoder refuses more. */
    static constexpr qsizetype maxPlayers = 1024;

    /*! \brief One player. */
    struct Entry
    {
        /*! The player's entity. */
        quint32 entityId = 0;
        /*! The player's name. */
        QString name;
        /*! Whether the player has operator rights. */
        bool operatorRights = false;
        /*! In spectator mode (decision E-730): listed, but shown apart. */
        bool spectator = false;
        /*! The round trip the player's client measured (decision E-886), smoothed, in
            microseconds; -1 while unknown. */
        qint32 latencyUs = -1;

        friend bool operator==(const Entry &, const Entry &) = default;
    };
    /*! The players. */
    QList<Entry> players;
};

} // namespace QcMsg
