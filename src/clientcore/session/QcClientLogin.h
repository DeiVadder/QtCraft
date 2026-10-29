// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/protocol/QcLoginMessages.h"

#include <QByteArray>
#include <QList>
#include <QString>

// The client's side of the login of a network game (decision E-252): answers the server's
// challenge with proofs of the password and of the player's identity for this server, the content
// packs and the wanted view distance, and keeps what the server told about itself.
class QcClientLogin
{
public:
    struct Credentials
    {
        // The installation key (QcPlayerKey); empty: a random key for this session only (the server
        // then knows the player only until the name is used with another key).
        QByteArray installationKey;
        QString password;
        // The client's content (QcContentFingerprint); the server refuses when they differ.
        QList<QcPackFingerprint> packs;
        // Chunks to stream (0: the server's limit).
        int viewDistance = 0;
    };

    void setCredentials(Credentials credentials);
    QcMsg::LoginResponse answer(const QcMsg::LoginChallenge &challenge);

    // What the server sent with its challenge; empty before (and for the in-process host).
    const QString &serverName() const { return m_serverName; }
    const QString &motd() const { return m_motd; }
    // The server asked for a password, the client had none.
    bool passwordMissing() const { return m_passwordMissing; }

private:
    Credentials m_credentials;
    QString m_serverName;
    QString m_motd;
    bool m_passwordMissing = false;
};
