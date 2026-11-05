// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtGlobal>

#include <optional>

class QcServerPersistence;
class QcServerState;

/*! \brief When the server saves on its own: autosaves and the identities of new players.

    Both count in the players' time (QcServerClock::playerTimeNs), so neither a high tick rate nor
    a sprint saves more often (decision E-249). */
class QcSaveSchedule
{
public:
    QcSaveSchedule(QcServerState &state, QcServerPersistence &persistence);

    /*! Tells everyone about a new failure of a background save, then starts an autosave when the
        interval has passed or many unloaded chunks wait (decision E-374); one save runs at a time.
    */
    void autosave();
    /*! Starts a save in the background like an autosave unless one runs: the server is empty and
        pauses (decision E-877). */
    void saveBeforePause();
    /*! Writes identities.json when players registered and the last write is a second ago
        (decision E-273). */
    void flushIdentitiesWhenDue();
    /*! Writes identities.json now; failures are logged by the store. */
    void flushIdentities();
    /*! Returns the autosaves since the server started, saves before a pause included. */
    quint64 autosaveCount() const { return m_autosaves; }

private:
    // Starts a background save and counts it as an autosave.
    void startSave();

    QcServerState &m_state;
    QcServerPersistence &m_persistence;
    // Players' time of the latest autosave.
    qint64 m_lastAutosaveNs = 0;
    // Players' time of the latest flush of the identities; none yet: the first registration is
    // written with the next tick.
    std::optional<qint64> m_lastIdentityFlushNs;
    quint64 m_autosaves = 0;
};
