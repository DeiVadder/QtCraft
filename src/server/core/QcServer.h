#pragma once

#include "QcBlockScreens.h"
#include "QcBodyBurning.h"
#include "QcBlockSimulation.h"
#include "QcBlockUpdateQueue.h"
#include "QcChunkLoading.h"
#include "QcChunkStreaming.h"
#include "QcEntitySync.h"
#include "QcEnvironmentDamage.h"
#include "QcFireIgnition.h"
#include "QcLightningStrike.h"
#include "QcMessages.h"
#include "QcPlayerHealth.h"
#include "QcPlayerJoin.h"
#include "QcServerAnimals.h"
#include "QcServerBlocks.h"
#include "QcServerCircuits.h"
#include "QcServerCombat.h"
#include "QcServerCommands.h"
#include "QcServerCrafting.h"
#include "QcServerExperience.h"
#include "QcServerCreatures.h"
#include "QcServerFallingBlocks.h"
#include "QcServerDebug.h"
#include "QcServerFire.h"
#include "QcServerWeather.h"
#include "QcSnowfall.h"
#include "QcServerGameModes.h"
#include "QcServerInteraction.h"
#include "QcServerInventory.h"
#include "QcServerItems.h"
#include "QcServerLogin.h"
#include "QcServerMachines.h"
#include "QcServerMinecarts.h"
#include "QcServerPersistence.h"
#include "QcServerPlants.h"
#include "QcServerSounds.h"
#include "QcServerStands.h"
#include "QcServerTrading.h"
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
    [[nodiscard]] QcStatus openWorld();
    // Writes all players, world info, changed chunks, entities, machines, chests and pending
    // updates. A failing part does not keep the others from being written; the first error is
    // returned. No-op without a world directory.
    [[nodiscard]] QcStatus save();
    void start();
    // Saves, then disconnects every client; returns the result of saving.
    [[nodiscard]] QcStatus stop();
    // Where a client connects from. Local clients are those of the in-process host (single player:
    // the one who opened the world, who is the operator anyway); they join without a login and their
    // chat lines and commands are not rate limited (decision E-091, supplement). Everything else is
    // Remote: it logs in (decision E-252) and is fully limited.
    enum class ClientOrigin { Remote, Local };
    void addClient(std::unique_ptr<QcTransport> transport, ClientOrigin origin = ClientOrigin::Remote);

    int clientCount() const { return int(m_state.clients().size()); }
    static constexpr double playerMaxHealth = QcServerClient::maxHealth;
    quint64 tickCount() const { return m_state.clock().tick(); }
    QcServerWorld &world() { return m_state.world(); }
    // Every entity of the world (decision E-364) and the tables of the kinds.
    QcEntities &entities() { return m_state.entities(); }
    const QcEntities &entities() const { return m_state.entities(); }
    QcMobManager &mobs() { return m_state.entities().creatures(); }
    QcItemEntityManager &items() { return m_state.entities().items(); }
    QcPlantManager &plants() { return m_state.plants(); }
    // State that belongs to single blocks (decision E-357) and the rules of machines.
    QcBlockEntities &blockEntities() { return m_state.blockEntities(); }
    const QcMachineRules &machineRules() const { return m_state.machineRules(); }
    QcLiquidFlow &liquids() { return m_state.liquids(); }
    QcFallingBlockManager &fallingBlocks() { return m_state.entities().fallingBlocks(); }
    // Fire (decision E-392): the queue of the burning fires and their step in the tick.
    QcFireQueue &fires() { return m_state.world().fires(); }
    QcServerFire &fire() { return m_fire; }
    // Flint and steel, dispensers and lava lighting fire (decision E-393).
    QcFireIgnition &ignition() { return m_ignition; }
    QcBodyBurning &burning() { return m_burning; }
    // The weather, where it rains and snows, and lightning (decision E-395).
    QcServerWeather &weather() { return m_weather; }
    const QcLightningStrike &lightning() const { return m_lightning; }
    const QcSnowfall &snowfall() const { return m_snowfall; }
    QcArmourStandManager &stands() { return m_state.entities().stands(); }
    QcMinecartManager &carts() { return m_state.entities().minecarts(); }
    QcServerMinecarts &minecarts() { return m_minecarts; }
    // Experience orbs and the players' experience (decision E-398).
    QcExperienceOrbManager &orbs() { return m_state.entities().orbs(); }
    QcServerExperience &experience() { return m_experience; }
    // Signal Dust circuits (decision E-335).
    QcServerCircuits &circuits() { return m_circuits; }
    QcCircuitState &circuitState() { return m_state.circuits(); }
    QcHopperQueue &hopperQueue() { return m_state.hoppers(); }
    // The contents of the container or machine at `pos` changed (set by a test or tool): comparators
    // reading it and hoppers next to it look again.
    void noteContents(QcBlockPos pos) { m_state.noteContents(pos); }
    const QcMovingBlocks &movingBlocks() const { return m_state.entities().movingBlocks(); }
    bool keepInventory() const { return m_state.info().keepInventory; }
    void setKeepInventory(bool keep) { m_state.info().keepInventory = keep; }
    double worldTime() const { return m_state.info().worldTime; }
    // Problems found while opening the world (missing mods, unknown blocks); it still opened.
    const QStringList &openWarnings() const { return m_persistence.openWarnings(); }
    // Whether openWorld() failed because another server has the world open.
    bool worldInUse() const { return m_persistence.worldInUse(); }
    // Whether openWorld() failed because the world comes from an older development version.
    bool worldTooOld() const { return m_persistence.worldTooOld(); }
    // Saves in the background like an autosave (tests and measurements).
    void saveInBackground() { m_persistence.saveInBackground(); }
    // Which chunks are loaded and how they come and go (decisions E-374 to E-377).
    QcChunkLoading &chunkLoading() { return m_chunkLoading; }
    // Waits until everything handed to the world's storage is written (autosaves run in the
    // background, decision E-373).
    void waitForStorage() { m_persistence.waitForStorage(); }
    // Whether openWorld() failed on a damaged per-region file, and which.
    const QcServerPersistence::DamagedFile &damagedFile() const { return m_persistence.damagedFile(); }
    void setWorldTime(double seconds);

    static constexpr double attackReach = QcServerCombat::attackReach;
    static constexpr double playerAttackCooldown = QcServerCombat::playerAttackCooldown;
    static constexpr double fallDamageThreshold = QcPlayerHealth::fallDamageThreshold;
    static constexpr double craftingTableReach = QcServerCrafting::craftingTableReach;
    static constexpr int craftingGridSlot = QcServerInventory::craftingGridSlot;
    const QcServerConfig &config() const { return m_state.config(); }

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
    // Everyone on the server to every player (QcMsg::PlayerList), after joins, leaves and changes
    // of operator rights.
    void sendPlayerList();
    void handleInput(Client &client, const QcMsg::PlayerInput &message);
    // Replaces unknown outfits and hair styles with defaults; names come from the content.
    void handleSetAppearance(Client &client, const QcAppearance &appearance);
    void sendTime(Client &client);
    void sendTimeToAll();
    // Tells the operators of chunks found damaged (decision E-373).
    void reportDamagedChunks();
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
    QcServerFallingBlocks m_fallingBlocks;
    QcServerFire m_fire;
    QcServerWeather m_weather;
    QcBlockSimulation m_blockSimulation;
    QcServerCircuits m_circuits;
    QcBlockScreens m_screens;
    QcServerMachines m_machines;
    QcServerInventory m_inventory;
    QcServerCrafting m_crafting;
    QcFireIgnition m_ignition;
    QcPlayerHealth m_health;
    QcServerGameModes m_gameModes;
    QcServerStands m_stands;
    QcServerMinecarts m_minecarts;
    QcServerAnimals m_animals;
    QcServerPlants m_plants;
    QcServerInteraction m_interaction;
    QcServerItems m_items;
    QcServerExperience m_experience;
    QcServerCombat m_combat;
    QcServerCreatures m_creatures;
    QcServerTrading m_trading;
    QcEnvironmentDamage m_environment;
    QcBodyBurning m_burning;
    QcLightningStrike m_lightning;
    QcSnowfall m_snowfall;
    QcEntitySync m_entitySync;
    QcChunkLoading m_chunkLoading;
    QcChunkStreaming m_chunkStreaming;
    QcServerPersistence m_persistence;
    QcServerCommands m_commands;
    QcPlayerJoin m_join;
    QcServerLogin m_login;
    // Single shot shortly before the next due tick, in the same event loop as the messages of the
    // clients.
    QTimer *m_tickTimer = nullptr;
    // Players' time (QcServerClock::playerTimeNs) of the latest autosave and sprint sync.
    qint64 m_lastAutosaveNs = 0;
    static constexpr qint64 identityFlushIntervalNs = 1'000'000'000;
    // The first registration is written with the next tick.
    qint64 m_lastIdentityFlushNs = -identityFlushIntervalNs;
    qint64 m_lastSprintSyncNs = 0;
    quint64 m_autosaves = 0;
    QcServerDebug m_debug;
};
