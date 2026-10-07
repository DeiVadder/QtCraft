#pragma once

#include "QcContentRegistry.h"
#include "QcMessages.h"
#include "QcServerConfig.h"
#include "QcServerWorld.h"

#include <QObject>

#include <memory>
#include <vector>

class QcConnection;
class QcTransport;
class QTimer;

// Authoritative game server. Owns the world and all game logic and talks to clients only through
// protocol messages. Lives in one thread; all functions must be called from that thread.
class QcServer : public QObject
{
    Q_OBJECT

public:
    QcServer(QcServerConfig config, std::shared_ptr<const QcContentRegistry> content,
             std::unique_ptr<QcWorldGenerator> generator, QObject *parent = nullptr);
    ~QcServer() override;

    void start();
    void stop();
    void addClient(std::unique_ptr<QcTransport> transport);

    int clientCount() const { return int(m_clients.size()); }
    quint64 tickCount() const { return m_tickCount; }
    QcServerWorld &world() { return m_world; }
    const QcServerConfig &config() const { return m_config; }

    // Runs one simulation tick immediately (used by tests and by the tick timer).
    void tick();

signals:
    void clientCountChanged(int count);

private:
    struct Client;

    void handleMessage(Client &client, QcMessage &&message);
    void handleHello(Client &client, const QcMsg::Hello &hello);
    void handleInput(Client &client, const QcMsg::PlayerInput &message);
    void streamChunks(Client &client);
    void removeClosedClients();

    QcServerConfig m_config;
    std::shared_ptr<const QcContentRegistry> m_content;
    QcServerWorld m_world;
    std::vector<std::unique_ptr<Client>> m_clients;
    QTimer *m_tickTimer = nullptr;
    quint64 m_tickCount = 0;
    quint32 m_nextEntityId = 1;
};
