// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/mobs/QcAppearance.h"
#include "common/core/QcVec3.h"
#include "common/gameplay/QcItemStack.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>

class QcClientSession;

/*! \brief JSON views of the session state that control responses report. */
class QcControlReports
{
public:
    /*! \a session: the session reported on; \a chatLog: chat lines received while the current
        request runs. */
    QcControlReports(const QcClientSession &session, const QStringList &chatLog);

    /*! Returns \a v as [x, y, z]. */
    static QJsonArray vectorJson(const QcVec3 &v);
    /*! Returns the player's \a look with the colours as "#rrggbb". */
    static QJsonObject appearanceJson(const QcAppearance &look);

    /*! Returns the player's status: position, motion, health, target and the chat lines of the
        request. */
    QJsonObject playerStatus() const;
    /*! Returns \a stack as {item, count, ...}; null for an empty stack or an unknown item. */
    QJsonValue stackJson(const QcItemStack &stack) const;
    /*! Returns the inventory: the occupied slots, the selected one and the crafting grid. */
    QJsonObject inventoryJson() const;
    /*! Returns the open machine screen: {"open": false} while none is open. */
    QJsonObject machineJson() const;
    /*! Returns the recent sound cues with their loudness and side as heard now, and the ambience
        levels. */
    QJsonObject soundsJson() const;
    /*! Returns the weather where the player stands (decision E-395). */
    QJsonObject weatherJson() const;

private:
    // The block the player aims at, none when they aim at nothing.
    [[nodiscard]] std::optional<QJsonObject> targetJson() const;

    const QcClientSession &m_session;
    const QStringList &m_chatLog;
};
