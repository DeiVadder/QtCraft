// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QJsonObject>

class QcClientSession;
class QcControlCommandTable;
class QcControlContext;

// Control commands that turn, walk and wait.
class QcControlMovementCommands
{
public:
    explicit QcControlMovementCommands(QcControlContext &context);

    void registerCommands(QcControlCommandTable &table);

private:
    QcClientSession &session() const;

    void look(const QJsonObject &request);
    void lookAt(const QJsonObject &request);
    // `move`, and `jump` with `isJump`.
    void move(const QJsonObject &request, bool isJump);
    void wait(const QJsonObject &request);
    void waitUntilReady(const QJsonObject &request);
    // {"cmd":"fly","on":true|false}: the wish to fly (decision E-284), as after a double tap.
    void fly(const QJsonObject &request);
    // {"cmd":"gamemode"} reports the mode, with "mode" it asks the server (/gamemode, an operator
    // command) and waits until the mode arrived.
    void gamemode(const QJsonObject &request);
    // Whether the session plays, the 3 x 3 chunks around the player are loaded and the player has settled.
    bool isReady() const;

    QcControlContext &m_context;
};
