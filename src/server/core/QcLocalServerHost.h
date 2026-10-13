#pragma once

#include "QcMailbox.h"
#include "QcNetworkServer.h"
#include "QcResult.h"
#include "QcServerConfig.h"
#include "QcTickStats.h"
#include "QcTransport.h"

#include <QObject>

#include <memory>
#include <optional>

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

    // Connects a new client; the returned transport belongs to the caller's thread. Clients of the
    // host are local players (QcServer::ClientOrigin::Local: chat is not rate limited, debug data
    // is sent).
    std::unique_ptr<QcTransport> connectClient();
    // Saves the world now (blocks until done).
    QcStatus save();
    // Lets network clients join as well (decision E-251): a listener in the server thread, the
    // local player stays in-process. Returns the port in use; listening again replaces the listener.
    // `login` replaces the login settings of the server's configuration.
    QcResult<quint16> listen(const QcNetworkConfig &config, const std::optional<QcLoginSettings> &login = std::nullopt);
    // Closes the listener and disconnects the network players ("The host stopped sharing the
    // world"); the local player plays on.
    void stopListening();
    // The port of the listener, 0 when not listening.
    quint16 listeningPort() const { return m_listeningPort; }
    // Runs a line of the server console in the server thread (decision E-247) and returns the
    // replies as English text (blocks until done).
    QStringList runConsoleCommand(const QString &line);
    // Server tick durations, lateness and skipped ticks so far (benchmark); zero when no world is open.
    QcTickStats::Summary tickStats() const;
    void resetTickTimes();
    // Creatures and dropped item stacks on the server now (benchmark; blocks until the server
    // thread counted them). Both -1 when no world is open.
    struct EntityCounts
    {
        qsizetype mobs = -1;
        qsizetype items = -1;
    };
    EntityCounts entityCounts();
    // Stops the server (which saves) and joins its thread. Safe to call more than once; later
    // calls report success.
    QcStatus shutdown();

signals:
    // The console asked to stop the server (/stop); the owner shuts the host down.
    void stopRequested();

private:
    // Runs in the server thread.
    void acceptPendingClients(QcServer *server);

    QThread *m_thread = nullptr;
    QString m_startError;
    bool m_worldInUse = false;
    QcMailbox<std::unique_ptr<QcTransport>> m_pendingClients;
    QcServer *m_server = nullptr;
    // Lives in the server thread as a child of the server; only touched there.
    QcNetworkServer *m_network = nullptr;
    quint16 m_listeningPort = 0;
};
