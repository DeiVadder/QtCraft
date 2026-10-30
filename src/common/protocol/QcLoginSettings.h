// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/protocol/QcLoginMessages.h"

#include <QList>
#include <QString>

/*! \brief The login settings of a server open to network players (decision E-252); a
    single-player world opened to the LAN gets them when it starts listening. */
struct QcLoginSettings
{
    /*! The name clients show for the server. */
    QString serverName = QStringLiteral("QtCraft server");
    /*! The message of the day clients show; may be empty. */
    QString motd;
    /*! Empty: no password. */
    QString password;
    /*! Players on the server at most, the local player included. */
    int maxPlayers = 8;
    /*! The content packs a client must have (QcContentFingerprint, with hashes); empty: id and
        version of the packs of the loaded content. */
    QList<QcPackFingerprint> contentPacks;
};
