#pragma once

#include "QcBackupRunner.h"
#include "QcCommandRegistry.h"
#include "QcItemStack.h"
#include "QcVec3.h"

#include <QElapsedTimer>
#include <QString>

#include <functional>
#include <optional>

class QcPendingCommands;
class QcTickPhaseTimer;
class QcTickScheduler;
struct QcServerState;

// Maintenance of the world (decisions E-245, E-246): saving, backups, the world's numbers, removing
// entities and stopping the server.
class QcWorldCommands
{
public:
    // Default and largest radius of /remove (metres).
    static constexpr int defaultRemoveRadius = 32;
    static constexpr int maxRemoveRadius = 256;
    // Removals of more entities than this (and every removal of stands or in the whole world) ask
    // for /confirm first.
    static constexpr int confirmRemoveCount = 16;

    struct Parts
    {
        const QcTickPhaseTimer &phases;
        const QcTickScheduler &ticks;
        std::function<QcStatus()> save;
        // Asks the host to stop the server (console only).
        std::function<void()> stop;
    };

    QcWorldCommands(QcServerState &state, QcPendingCommands &pending, Parts parts);

    void addTo(QcCommandRegistry &registry);
    // Reports a finished background backup (every tick) and waits for a running one (server stop).
    void pollBackup() { m_backups.poll(); }
    void waitForBackup() { m_backups.wait(); }
    QcBackupRunner &backups() { return m_backups; }

private:
    // What /remove takes away.
    struct Removal
    {
        QString what;
        std::optional<QcVec3> centre;
        double radius = 0.0;
        int mobs = 0;
        int items = 0;
        int stands = 0;
    };

    void save(const QcCommandContext &context) const;
    void backup(const QcCommandContext &context);
    // Tells the player who asked (when still there) and the log how the copy ended.
    void reportBackup(quint32 requester, const QcResult<QcWorldBackup::Summary> &result, qint64 startedMs) const;
    void worldInfo(const QcCommandContext &context) const;
    void remove(const QcCommandContext &context);
    // Counts (or with `apply` removes) what the removal matches.
    void sweep(Removal &removal, bool apply);
    // Selectors: mobs, hostile, animals, ambient, items, stands, all, a creature name or "item:<name>".
    bool isKnownSelector(const QString &what) const;
    bool matchesMob(const QString &what, int type) const;
    bool matchesItem(const QString &what, QcItemStack stack) const;

    QcServerState &m_state;
    QcPendingCommands &m_pending;
    Parts m_parts;
    QcBackupRunner m_backups;
    // Real time for the duration of a backup.
    QElapsedTimer m_clock;
};
