#pragma once

#include "QcBlockSimulation.h"
#include "QcBlockUpdateQueue.h"
#include "QcChatCommands.h"
#include "QcChunkStreaming.h"
#include "QcEntitySync.h"
#include "QcMessages.h"
#include "QcServerBlocks.h"
#include "QcServerCombat.h"
#include "QcServerInteraction.h"
#include "QcServerInventory.h"
#include "QcServerItemsAndPlants.h"
#include "QcServerMachines.h"
#include "QcServerPersistence.h"
#include "QcServerSounds.h"
#include "QcServerState.h"
#include "QcTickScheduler.h"

#include <QObject>

#include <memory>

class QcConnection;
class QcTransport;
class QTimer;

// Authoritative game server. Owns the world and all game logic and talks to clients only through
// protocol messages: it runs the tick and hands messages to its parts. Lives in one thread; all
// functions must be called from that thread.
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
    // Writes all players, world info, changed chunks, entities, machines, chests and pending
    // updates. A failing part does not keep the others from being written; the first error is
    // returned. No-op without a world directory.
    QcStatus save();
    void start();
    // Saves, then disconnects every client; returns the result of saving.
    QcStatus stop();
    void addClient(std::unique_ptr<QcTransport> transport);

    int clientCount() const { return int(m_state.clients.size()); }
    static constexpr double playerMaxHealth = QcServerClient::maxHealth;
    quint64 tickCount() const { return m_state.tickCount; }
    QcServerWorld &world() { return m_state.world; }
    QcMobManager &mobs() { return m_state.mobs; }
    QcItemEntityManager &items() { return m_state.items; }
    QcPlantManager &plants() { return m_state.plants; }
    QcMachineManager &machines() { return m_state.machines; }
    QcContainerManager &containers() { return m_state.containers; }
    QcLiquidFlow &liquids() { return m_state.liquids; }
    QcFallingBlockManager &fallingBlocks() { return m_state.falling; }
    bool keepInventory() const { return m_state.info.keepInventory; }
    void setKeepInventory(bool keep) { m_state.info.keepInventory = keep; }
    double worldTime() const { return m_state.info.worldTime; }
    // Problems found while opening the world (missing mods, unknown blocks); it still opened.
    const QStringList &openWarnings() const { return m_persistence.openWarnings(); }
    // Whether openWorld() failed because another server has the world open.
    bool worldInUse() const { return m_persistence.worldInUse(); }
    void setWorldTime(double seconds);

    static constexpr double attackReach = QcServerCombat::attackReach;
    static constexpr double playerAttackCooldown = QcServerCombat::playerAttackCooldown;
    static constexpr double fallDamageThreshold = QcServerCombat::fallDamageThreshold;
    static constexpr double craftingTableReach = QcServerInventory::craftingTableReach;
    static constexpr int craftingGridSlot = QcServerInventory::craftingGridSlot;
    const QcServerConfig &config() const { return m_state.config; }

    // Runs one simulation tick immediately (used by tests and by the tick timer).
    void tick();

    static constexpr int maxBlockUpdatesPerTick = QcBlockSimulation::maxBlockUpdatesPerTick;
    static constexpr qint64 maxFillBlocks = QcChatCommands::maxFillBlocks;
    // Queued updates of loaded chunks (the next ticks work on them).
    qsizetype pendingBlockUpdates() const { return m_blockUpdates.pendingPlayerCount() + m_blockUpdates.pendingCommandCount(); }
    // Updates of positions in chunks that are not loaded.
    qsizetype waitingBlockUpdates() const { return m_blockUpdates.waitingCount(); }
    // Updates waiting in the queue of player actions and in the queue of commands.
    qsizetype pendingPlayerBlockUpdates() const { return m_blockUpdates.pendingPlayerCount(); }
    qsizetype pendingCommandBlockUpdates() const { return m_blockUpdates.pendingCommandCount(); }
    quint64 processedBlockUpdates() const { return m_blockUpdates.processedCount(); }
    // Command updates dropped because the command queue was full (config.maxCommandBlockUpdates).
    quint64 droppedBlockUpdates() const { return m_blockUpdates.droppedCount(); }

    // Duration and lateness of the timed ticks; safe to read and reset from other threads (benchmark).
    QcTickStats::Summary tickTimes() const { return m_ticks.stats().summary(); }
    void resetTickTimes() { m_ticks.stats().reset(); }

signals:
    void clientCountChanged(int count);

private:
    using Client = QcServerClient;

    void handleMessage(Client &client, QcMessage &&message);
    // Pays the message's token (decision E-091); false when it is dropped.
    bool withinRateLimit(Client &client, const QcMessage &message);
    // Sends the true block of a dropped StartMining or FinishMining back, so a client that predicted
    // the break does not keep a phantom gap; once per position and tick, a few positions per tick.
    void resendDroppedMiningTarget(Client &client, const QcMessage &message);
    // Messages about moving, mining, using and fighting, chat and leaving; false for other types.
    bool handlePlayerMessage(Client &client, const QcMessage &message);
    // Messages about the inventory, crafting, machine screens and the look; false for other types.
    bool handleInventoryMessage(Client &client, const QcMessage &message);
    void handleHello(Client &client, const QcMsg::Hello &hello);
    // Restores a saved player; false when there is none (or no storage). Closes the connection
    // when the saved data cannot be read.
    bool restorePlayer(Client &client);
    void sendWelcome(Client &client, bool rejoined);
    void handleInput(Client &client, const QcMsg::PlayerInput &message);
    // Replaces unknown outfits and hair styles with defaults; names come from the content.
    QcAppearance validAppearance(const QcAppearance &requested) const;
    void handleSetAppearance(Client &client, const QcAppearance &appearance);
    void sendOpenWarnings(Client &client);
    void sendTime(Client &client);
    void autosave();
    // Chunks, entities, time, food and the player's own state for one client after a tick.
    void updateClient(Client &client);
    // Timer slot: runs the due ticks and plans the next as a single shot (decision E-090).
    void runDueTicks();
    // Waits for the next tick when it is due within timerLeadNs (the timer fires that early).
    void waitUntilDue() const;
    // Arms the single shot timerLeadNs before the next tick is due, `remainingNs` from now.
    void armTickTimer(qint64 remainingNs);
    void removeClosedClients();

    QcServerState m_state;
    QcServerSounds m_sounds;
    QcBlockUpdateQueue m_blockUpdates;
    QcServerBlocks m_blocks;
    QcBlockSimulation m_blockSimulation;
    QcServerMachines m_machineScreens;
    QcServerInventory m_inventory;
    QcServerInteraction m_interaction;
    QcServerItemsAndPlants m_itemsAndPlants;
    QcServerCombat m_combat;
    QcEntitySync m_entitySync;
    QcChunkStreaming m_chunkStreaming;
    QcChatCommands m_chat;
    QcServerPersistence m_persistence;
    // Single shot shortly before the next due tick, in the same event loop as the messages of the
    // clients.
    QTimer *m_tickTimer = nullptr;
    QcTickScheduler m_ticks;
};
