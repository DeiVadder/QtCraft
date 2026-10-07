#pragma once

#include "QcContentRegistry.h"
#include "QcMessages.h"
#include "QcServerConfig.h"
#include "QcServerWorld.h"
#include "QcWorldStorage.h"

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

    // Opens or creates the world directory given in the config (no-op without one). Must succeed
    // before start(); fails with a readable message for damaged or mismatching saves.
    QcStatus openWorld();
    // Writes world info, changed chunks and all players. No-op without a world directory.
    QcStatus save();
    void start();
    // Saves, then disconnects every client; returns the result of saving.
    QcStatus stop();
    void addClient(std::unique_ptr<QcTransport> transport);

    int clientCount() const { return int(m_clients.size()); }
    // Ticks of slack when checking a client's claim that a block is broken (network jitter).
    static constexpr int miningToleranceTicks = 2;
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
    void handleFinishMining(Client &client, QcBlockPos pos);
    void handleUseItem(Client &client, QcMsg::UseItem message);
    void handleChat(Client &client, const QString &text);
    bool isWithinReach(const Client &client, QcBlockPos pos) const;
    void setBlockAndBroadcast(QcBlockPos pos, QcBlockId block);
    void sendInventory(Client &client);
    const QcItemDefinition *heldItem(const Client &client) const;
    void streamChunks(Client &client);
    void removeClosedClients();
    void savePlayer(const Client &client);

    QcServerConfig m_config;
    std::shared_ptr<const QcContentRegistry> m_content;
    QcServerWorld m_world;
    std::vector<std::unique_ptr<Client>> m_clients;
    QTimer *m_tickTimer = nullptr;
    quint64 m_tickCount = 0;
    quint32 m_nextEntityId = 1;
    std::optional<QcWorldStorage> m_storage;
    QcWorldInfo m_info;
};
