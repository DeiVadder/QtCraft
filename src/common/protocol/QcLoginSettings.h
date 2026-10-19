#pragma once

#include "common/protocol/QcLoginMessages.h"

#include <QList>
#include <QString>

// The login settings of a server open to network players (decision E-252); a single-player world
// opened to the LAN gets them when it starts listening.
struct QcLoginSettings
{
    QString serverName = QStringLiteral("QtCraft server");
    QString motd;
    // Empty: no password.
    QString password;
    // Players on the server at most, the local player included.
    int maxPlayers = 8;
    // The content packs a client must have (QcContentFingerprint, with hashes); empty: id and
    // version of the packs of the loaded content.
    QList<QcPackFingerprint> contentPacks;
};
