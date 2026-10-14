#pragma once

#include "QcBlockSimulation.h"
#include "QcBlockUpdateQueue.h"
#include "QcChunkStreaming.h"
#include "QcEntitySync.h"
#include "QcMessages.h"
#include "QcServerBlocks.h"
#include "QcServerCircuits.h"
#include "QcServerDebug.h"
#include "QcServerLogin.h"
#include "QcServerCombat.h"
#include "QcServerFarming.h"
#include "QcServerGameModes.h"
#include "QcServerInteraction.h"
#include "QcServerInventory.h"
#include "QcServerStands.h"
#include "QcServerItems.h"
#include "QcServerPlants.h"
#include "QcBlockScreens.h"
#include "QcServerMachines.h"
#include "QcServerCommands.h"
#include "QcServerPersistence.h"
#include "QcServerSounds.h"
#include "QcServerState.h"
#include "QcTickControl.h"
#include "QcTickPhaseTimer.h"
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
    // Where a client connects from. Local clients are those of the in-process host (single player:
    // the one who opened the world, who is the operator anyway); they join without a login and their
    // chat lines and commands are not rate limited (decision E-091, supplement). Everything else is
    // Remote: it logs in (decision E-252) and is fully limited.
    enum class ClientOrigin { Remote, Local };
    void addClient(std::unique_ptr<QcTransport> transport, ClientOrigin origin = ClientOrigin::Remote);

    int clientCount() const { return int(m_state.clients.size()); }
    static constexpr double playerMaxHealth = QcServerClient::maxHealth;
    quint64 tickCount() const { return m_state.tickCount; }
    QcServerWorld &world() { return m_state.world; }
    QcMobManager &mobs() { return m_state.mobs; }
    QcItemEntityManager &items() { return m_state.items; }
    QcPlantManager &plants() { return m_state.plants; }
    // State that belongs to single blocks (decision E-357) and the rules of machines.
    QcBlockEntities &blockEntities() { return m_state.blockEntities(); }
    const QcMachineRules &machineRules() const { return m_state.machineRules; }
    QcLiquidFlow &liquids() { return m_state.liquids; }
    QcFallingBlockManager &fallingBlocks() { return m_state.falling; }
    QcArmourStandManager &stands() { return m_state.stands; }
    // Signal Dust circuits (decision E-335).
    QcServerCircuits &circuits() { return m_circuits; }
    QcCircuitState &circuitState() { return m_state.circuits; }
    QcHopperQueue &hopperQueue() { return m_state.hoppers; }
    // The contents of the container or machine at `pos` changed (set by a test or tool): comparators
    // reading it and hoppers next to it look again.
    void noteContents(QcBlockPos pos) { m_state.noteContents(pos); }
    const QcMovingBlocks &movingBlocks() const { return m_state.movingBlocks; }
    bool keepInventory() const { return m_state.info.keepInventory; }
    void setKeepInventory(bool keep) { m_state.info.keepInventory = keep; }
    double worldTime() const { return m_state.info.worldTime; }
    // Problems found while opening the world (missing mods, unknown blocks); it still opened.
    const QStringList &openWarnings() const { return m_persistence.openWarnings(); }
    // Whether openWorld() failed because another server has the world open.
    bool worldInUse() const { return m_persistence.worldInUse(); }
    // Whether openWorld() failed because the world comes from an older development version.
    bool worldTooOld() const { return m_persistence.worldTooOld(); }
    void setWorldTime(double seconds);

    static constexpr double attackReach = QcServerCombat::attackReach;
    static constexpr double playerAttackCooldown = QcServerCombat::playerAttackCooldown;
    static constexpr double fallDamageThreshold = QcServerCombat::fallDamageThreshold;
    static constexpr double craftingTableReach = QcServerInventory::craftingTableReach;
    static constexpr int craftingGridSlot = QcServerInventory::craftingGridSlot;
    const QcServerConfig &config() const { return m_state.config; }

    // Runs one simulation tick immediately (used by tests and by the tick timer).
    void tick();
    // Autosaves since the server started.
    quint64 autosaveCount() const { return m_autosaves; }
    // Server ticks per tick of a client's clock, sent with WorldTime: the tick rate / 20, the
    // sprint's speed while one runs, 0 while frozen (decision E-249).
    double timeSpeed() const;

    static constexpr int maxBlockUpdatesPerTick = QcBlockSimulation::maxBlockUpdatesPerTick;
    static constexpr qint64 maxFillBlocks = QcBlockCommands::maxFillBlocks;
    // Queued updates of loaded chunks (the next ticks work on them).
    qsizetype pendingBlockUpdates() const { return m_blockUpdates.pendingPlayerCount() + m_blockUpdates.pendingCommandCount(); }
    // Updates of positions in chunks that are not loaded.
    qsizetype waitingBlockUpdates() const { return m_blockUpdates.waitingCount(); }
    // Updates waiting in the queue of player actions and in the queue of commands.
    qsizetype pendingPlayerBlockUpdates() const { return m_blockUpdates.pendingPlayerCount(); }
    qsizetype pendingCommandBlockUpdates() const { return m_blockUpdates.pendingCommandCount(); }
    quint64 processedBlockUpdates() const { return m_blockUpdates.processedCount(); }
    // Command updates dropped because the command queue was full (QcServerLimits::commandBlockUpdates).
    quint64 droppedBlockUpdates() const { return m_blockUpdates.droppedCount(); }

    // Duration and lateness of the timed ticks; safe to read and reset from other threads (benchmark).
    QcTickStats::Summary tickTimes() const { return m_ticks.stats().summary(); }
    void resetTickTimes() { m_ticks.stats().reset(); }
    // Time per phase of the latest ticks (decision E-225); server thread only.
    const QcTickPhaseTimer &tickPhases() const { return m_phases; }
    // Tick rate, sprints and freezing (/tick, decision E-249); server thread only.
    QcTickScheduler &tickScheduler() { return m_ticks; }
    const QcTickControl &tickControl() const { return m_tickControl; }

    // Changes the login settings for network players (a world opened to the LAN, decision E-252).
    void setLoginSettings(const QcLoginSettings &login);
    // Ends the connections of all network clients with a reason (the host stopped sharing the
    // world); the local player stays. They are saved with the next tick like any who leave.
    void disconnectRemoteClients();
    // Runs a line of the server console (decision E-247); the replies go to `output`.
    void runConsoleCommand(const QString &line, const QcCommandSource::Output &output);
    QcServerCommands &commands() { return m_commands; }

signals:
    void clientCountChanged(int count);
    // /stop from the console: the host should stop the server.
    void stopRequested();

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
    // Messages before the client plays: Hello, the login response and leaving; anything else ends
    // the connection.
    void handleLoginMessage(Client &client, const QcMessage &message);
    // The admitted client becomes a player: restored from his save or spawned (QcServerLogin::Join).
    void join(Client &client);
    // Restores a saved player; false when there is none (or no storage). Closes the connection
    // when the saved data cannot be read.
    bool restorePlayer(Client &client);
    // Everyone on the server to every player (QcMsg::PlayerList), after joins, leaves and changes
    // of operator rights.
    void sendPlayerList();
    void sendWelcome(Client &client, bool rejoined);
    void handleInput(Client &client, const QcMsg::PlayerInput &message);
    // Replaces unknown outfits and hair styles with defaults; names come from the content.
    void handleSetAppearance(Client &client, const QcAppearance &appearance);
    void sendOpenWarnings(Client &client);
    // Tells an operator who joins that access.json was damaged when the world opened.
    void sendAccessWarning(Client &client);
    void sendTime(Client &client);
    void sendTimeToAll();
    void autosave();
    // Writes identities.json when players registered since the last write (decision E-273).
    void flushIdentities();
    void flushIdentitiesWhenDue();
    // Block updates, liquids, creatures, items, plants, falling blocks and machines of one tick.
    void simulate();
    // What the clients get after this tick besides their own state: the world around them
    // (chunks, entities, debug data) and the time of day.
    struct ClientSync
    {
        bool world = true;
        bool time = false;
    };
    ClientSync clientSyncDue();
    void refillInputBudget(Client &client) const;
    // Input budget, the world and time as due, food and the player's own state for one client.
    void updateClient(Client &client, ClientSync sync);
    // Timer slot: runs the due ticks and plans the next as a single shot (decision E-090).
    void runDueTicks();
    // Waits for the next tick when it is due within timerLeadNs (the timer fires that early).
    void waitUntilDue() const;
    // Arms the single shot timerLeadNs before the next tick is due, `remainingNs` from now.
    void armTickTimer(qint64 remainingNs);
    void removeClosedClients();

    QcServerState m_state;
    QcTickScheduler m_ticks;
    QcTickPhaseTimer m_phases;
    QcTickControl m_tickControl;
    QcServerSounds m_sounds;
    QcBlockUpdateQueue m_blockUpdates;
    QcServerBlocks m_blocks;
    QcBlockSimulation m_blockSimulation;
    QcServerCircuits m_circuits;
    QcBlockScreens m_screens;
    QcServerMachines m_machines;
    QcServerInventory m_inventory;
    QcServerGameModes m_gameModes;
    QcServerStands m_stands;
    QcServerFarming m_farming;
    QcServerInteraction m_interaction;
    QcServerItems m_items;
    QcServerPlants m_plants;
    QcServerCombat m_combat;
    QcEntitySync m_entitySync;
    QcChunkStreaming m_chunkStreaming;
    QcServerPersistence m_persistence;
    QcServerCommands m_commands;
    QcServerLogin m_login;
    // Single shot shortly before the next due tick, in the same event loop as the messages of the
    // clients.
    QTimer *m_tickTimer = nullptr;
    // Players' time (QcServerState::playerTimeNs) of the latest autosave and sprint sync.
    qint64 m_lastAutosaveNs = 0;
    static constexpr qint64 identityFlushIntervalNs = 1'000'000'000;
    // The first registration is written with the next tick.
    qint64 m_lastIdentityFlushNs = -identityFlushIntervalNs;
    qint64 m_lastSprintSyncNs = 0;
    quint64 m_autosaves = 0;
    QcServerDebug m_debug;
};
