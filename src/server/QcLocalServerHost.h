#pragma once

#include "QcMailbox.h"
#include "QcServerConfig.h"
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

    // Connects a new client; the returned transport belongs to the caller's thread.
    std::unique_ptr<QcTransport> connectClient();
    // Stops the server and joins its thread. Safe to call more than once.
    void shutdown();

private:
    // Runs in the server thread.
    void acceptPendingClients(QcServer *server);

    QThread *m_thread = nullptr;
    QcMailbox<std::unique_ptr<QcTransport>> m_pendingClients;
    QcServer *m_server = nullptr;
};
