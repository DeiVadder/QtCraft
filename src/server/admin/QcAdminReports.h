// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/protocol/QcAdminMessages.h"

#include <QHash>
#include <QSet>
#include <QStringList>

class QcCommandRegistry;
class QcServerState;
class QcTickPhaseTimer;
class QcTickScheduler;

/*! \brief The reports of the admin channel (decision E-848), built from the server's state as it is
    now: status, players, command catalogue, access lists, settings and the log's lines.

    Each fits the bounds of its message (QcMsg::AdminLimits). */
class QcAdminReports
{
public:
    QcAdminReports(const QcServerState &state, const QcCommandRegistry &registry, const QcTickPhaseTimer &phases, const QcTickScheduler &ticks);

    /*! Returns the status around \a sample, with the server's uptime \a uptimeMs. */
    QcMsg::AdminStatus status(const QcMsg::AdminSample &sample, quint64 uptimeMs) const;
    /*! Returns everyone playing; \a pings holds the latest round trip by entity id. */
    QcMsg::AdminPlayers players(const QHash<quint32, qint32> &pings) const;
    /*! Returns every command with its group and names (at most AdminLimits::maxNames, logged
        once per command when cut). */
    QcMsg::AdminCatalogue catalogue() const;
    /*! Returns the access lists: whether the whitelist is on, the operators, the whitelist and the
        bans (each cut to AdminLimits::maxEntries). */
    QcMsg::AdminLists lists() const;
    /*! Returns the game rules with their values in this world and the server limits with their
        defaults and current values, and whether the limits come from the server. */
    QcMsg::AdminSettings settings() const;
    /*! \brief Log lines (QcMessageLog) and the number of the newest line looked at: the next
        call asks for the lines after it, so none comes twice. */
    struct Log
    {
        /*! The message with the lines. */
        QcMsg::AdminLog message;
        /*! The number of the newest line looked at (0 when there is none). */
        quint64 latest = 0;
    };
    /*! Returns the newest log lines of at least \a level after the line numbered \a after, at
        most \a count (and at most AdminLimits::maxLogLines); \a tail marks them as a tail that
        replaces what the client shows. A server in the game's process
        (QcServerConfig::fullAdminLog off) shows only the categories of the server
        (serverCategories()): the rest of the process's log is the host's game. */
    Log log(QcMsg::AdminLogLine::Level level, qsizetype count, quint64 after, bool tail) const;
    /*! Returns the logging categories of the server's side: commands, admin, game modes, items,
        lag, network, plants, server, trades, world generation. */
    static const QSet<QString> &serverCategories();
    /*! Returns the entities of the world by kind, all of them. */
    std::array<quint32, QcEntityKinds::count> entitiesByKind() const;

private:
    const QcServerState &m_state;
    const QcCommandRegistry &m_registry;
    const QcTickPhaseTimer &m_phases;
    const QcTickScheduler &m_ticks;
    // Commands whose names catalogue() cut to AdminLimits::maxNames and said so in the log (once
    // each, the catalogue is sent on every request).
    mutable QSet<QString> m_namesCut;
};
