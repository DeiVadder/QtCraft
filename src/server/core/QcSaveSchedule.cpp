// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/core/QcSaveSchedule.h"

#include "common/protocol/QcChatText.h"
#include "common/protocol/QcConnection.h"
#include "server/core/QcServerLogging.h"
#include "server/core/QcServerState.h"
#include "server/storage/QcServerPersistence.h"

namespace {

// New identities are written at most once a second, not with every login (decision E-273).
constexpr qint64 identityFlushIntervalNs = 1'000'000'000;

} // namespace

QcSaveSchedule::QcSaveSchedule(QcServerState &state, QcServerPersistence &persistence)
    : m_state(state)
    , m_persistence(persistence)
{
}

void QcSaveSchedule::autosave()
{
    // A failed save in the background is told to everyone once per streak of failures (review of 31.3).
    if (const std::optional<QcStatus> failed = m_persistence.takeNewFailure()) {
        qCWarning(lcServer) << "Autosave failed:" << failed->error();
        for (const auto &client : m_state.clients()) {
            client->connection->send(QcChatText::message(QT_TRANSLATE_NOOP("QcServerMessages", "Autosave failed: %1"), {failed->error()}));
        }
    }

    // Unloaded chunks waiting in memory beyond their limit are written early (decision E-374), unless
    // saving fails: then only the normal interval tries again. One save runs at a time.
    const qint64 intervalNs = static_cast<qint64>(m_state.config().autosaveSeconds) * 1'000'000'000;
    const bool due = intervalNs > 0 && m_state.clock().playerTimeNs() - m_lastAutosaveNs >= intervalNs;
    const bool early = m_persistence.manyUnsavedChunks() && !m_persistence.savesFailing();
    if (!m_persistence.hasStorage() || m_persistence.saveRunning() || (!due && !early)) {
        return;
    }
    startSave();
}

void QcSaveSchedule::saveBeforePause()
{
    if (m_persistence.hasStorage() && !m_persistence.saveRunning()) {
        startSave();
    }
}

void QcSaveSchedule::startSave()
{
    m_lastAutosaveNs = m_state.clock().playerTimeNs();
    ++m_autosaves;
    m_persistence.saveInBackground();
}

void QcSaveSchedule::flushIdentitiesWhenDue()
{
    const std::optional<qint64> last = m_lastIdentityFlushNs;
    const bool due = !last || m_state.clock().playerTimeNs() - *last >= identityFlushIntervalNs;
    if (m_state.identities().hasUnsavedChanges() && due) {
        flushIdentities();
    }
}

void QcSaveSchedule::flushIdentities()
{
    m_lastIdentityFlushNs = m_state.clock().playerTimeNs();
    // Failures are logged by the store; the registrations count until the restart.
    [[maybe_unused]] const QcStatus flushed = m_state.identities().flush();
}
