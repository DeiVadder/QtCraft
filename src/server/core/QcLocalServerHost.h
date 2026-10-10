#pragma once

#include "QcMailbox.h"
#include "QcResult.h"
#include "QcServerConfig.h"
#include "QcTickStats.h"
#include "QcTransport.h"

#include <QObject>

#include <memory>

class QcContentRegistry;
class QcServer;
class QcWorldGenerator;
class QThread;

// Runs a QcServer in its own thread inside the game process (single player) and hands out
// in-process client transports. Owned and used by the GUI/main thread.
class QcLocalServerHost : public QObject
{
    Q_OBJECT

public:
    QcLocalServerHost(QcServerConfig config, std::shared_ptr<const QcContentRegistry> content,
                      std::unique_ptr<QcWorldGenerator> generator, QObject *parent = nullptr);
    ~QcLocalServerHost() override;

    // Empty when the world opened; otherwise the reason, and the host serves no clients.
    QString startError() const { return m_startError; }
    // Whether the world could not open because another server (window or process) has it open.
    bool worldInUse() const { return m_worldInUse; }

    // Connects a new client; the returned transport belongs to the caller's thread.
    std::unique_ptr<QcTransport> connectClient();
    // Saves the world now (blocks until done).
    QcStatus save();
    // Server tick durations, lateness and skipped ticks so far (benchmark); zero when no world is open.
    QcTickStats::Summary tickStats() const;
    void resetTickTimes();
    // Stops the server (which saves) and joins its thread. Safe to call more than once; later
    // calls report success.
    QcStatus shutdown();

private:
    // Runs in the server thread.
    void acceptPendingClients(QcServer *server);

    QThread *m_thread = nullptr;
    QString m_startError;
    bool m_worldInUse = false;
    QcMailbox<std::unique_ptr<QcTransport>> m_pendingClients;
    QcServer *m_server = nullptr;
};
