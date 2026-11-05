// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/diagnostics/QcHealthSample.h"
#include "common/protocol/QcMessages.h"
#include "server/admin/QcServerAdmin.h"
#include "server/animals/QcServerAnimals.h"
#include "server/blocks/QcBlockSimulation.h"
#include "server/blocks/QcBlockUpdateQueue.h"
#include "server/blocks/QcFireIgnition.h"
#include "server/blocks/QcServerBlocks.h"
#include "server/blocks/QcServerFallingBlocks.h"
#include "server/blocks/QcServerFire.h"
#include "server/chunks/QcChunkLoading.h"
#include "server/chunks/QcChunkStreaming.h"
#include "server/commands/QcServerCommands.h"
#include "server/core/QcEntitySync.h"
#include "server/core/QcSaveSchedule.h"
#include "server/core/QcServerDebug.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/core/QcServerSystems.h"
#include "server/core/QcServerVehicles.h"
#include "server/explosions/QcServerExplosions.h"
#include "server/gameplay/QcBodyBurning.h"
#include "server/gameplay/QcEnvironmentDamage.h"
#include "server/gameplay/QcPlayerHealth.h"
#include "server/gameplay/QcServerCombat.h"
#include "server/gameplay/QcServerCrafting.h"
#include "server/gameplay/QcServerEffects.h"
#include "server/gameplay/QcServerGameModes.h"
#include "server/gameplay/QcServerInteraction.h"
#include "server/gameplay/QcServerInventory.h"
#include "server/gameplay/QcServerStands.h"
#include "server/golems/QcServerGolems.h"
#include "server/items/QcServerExperience.h"
#include "server/items/QcServerItems.h"
#include "server/login/QcPlayerJoin.h"
#include "server/login/QcServerLogin.h"
#include "server/machines/QcBlockScreens.h"
#include "server/machines/QcServerKitchen.h"
#include "server/machines/QcServerMachines.h"
#include "server/machines/QcServerWorkstations.h"
#include "server/mobs/QcServerCreatures.h"
#include "server/plants/QcServerPlants.h"
#include "server/projectiles/QcServerProjectiles.h"
#include "server/signal/QcServerCircuits.h"
#include "server/storage/QcServerPersistence.h"
#include "server/tick/QcClientSyncPace.h"
#include "server/tick/QcLagReport.h"
#include "server/tick/QcServerPause.h"
#include "server/tick/QcTickControl.h"
#include "server/tick/QcTickPhaseTimer.h"
#include "server/tick/QcTickScheduler.h"
#include "server/tick/QcTickSystems.h"
#include "server/villages/QcServerTrading.h"
#include "server/weather/QcLightningStrike.h"
#include "server/weather/QcServerWeather.h"
#include "server/weather/QcSnowfall.h"

#include <QObject>

#include <memory>

class QcConnection;
class QcTransport;
class QTimer;

/*! \brief Authoritative game server.

    Owns the world and all game logic and talks to clients only through protocol messages: it
    runs the tick and hands messages to its parts. Lives in one thread; all functions must be
    called from that thread, except tickTimes() and resetTickTimes(). */
class QcServer : public QObject
{
    Q_OBJECT

public:
    QcServer(QcServerConfig config, std::shared_ptr<const QcContentRegistry> content,
             std::unique_ptr<QcWorldGenerator> generator, QObject *parent = nullptr);
    ~QcServer() override;
    Q_DISABLE_COPY_MOVE(QcServer)

    /*! Opens or creates the world directory given in the config (no-op without one). Must succeed
        before start(); returns an error with a readable message for damaged or mismatching saves.
    */
    [[nodiscard]] QcStatus openWorld();
    /*! Writes all players, world info, changed chunks, entities, machines, chests and pending
        updates. A failing part does not keep the others from being written; returns the first
        error. No-op without a world directory. */
    [[nodiscard]] QcStatus save();
    /*! Starts the tick loop: the first tick is due one interval from now. */
    void start();
    /*! Saves, then disconnects every client; returns the result of saving. */
    [[nodiscard]] QcStatus stop();
    /*! \brief Where a client connects from.

        Local clients are those of the in-process host (single player: the one who opened the
        world, who is the operator anyway); they join without a login and their chat lines and
        commands are not rate limited (decision E-091, supplement). Everything else is Remote: it
        logs in (decision E-252) and is fully limited. */
    enum class ClientOrigin {
        /*! A network client. */
        Remote,
        /*! The player of the in-process host. */
        Local
    };
    /*! Adds a client connected through \a transport from \a origin; a remote one logs in first. */
    void addClient(std::unique_ptr<QcTransport> transport, ClientOrigin origin = ClientOrigin::Remote);

    /*! Returns the number of clients, those still logging in included. */
    int clientCount() const { return static_cast<int>(m_state.clients().size()); }
    /*! The health of a player at full health (QcServerClient::maxHealth). */
    static constexpr double playerMaxHealth = QcServerClient::maxHealth;
    /*! Returns the ticks counted since the server started. */
    quint64 tickCount() const { return m_state.clock().tick(); }
    /*! Returns the world: chunks, blocks and what belongs to them. */
    QcServerWorld &world() { return m_state.world(); }
    /*! Returns the maps of the world (decision E-799). */
    QcMapStore &maps() { return m_state.maps(); }
    /*! Returns every entity of the world (decision E-364) and the tables of the kinds. */
    QcEntities &entities() { return m_state.entities(); }
    /*! Returns every entity of the world and the tables of the kinds, read-only. */
    const QcEntities &entities() const { return m_state.entities(); }
    /*! Returns the table of the creatures. */
    QcMobManager &mobs() { return m_state.entities().creatures(); }
    /*! Returns the creatures in the tick: guardians and crumbling (decisions E-591, E-593) for
        tests. */
    const QcServerCreatures &creatures() const { return m_creatures; }
    /*! Returns the table of the dropped items. */
    QcItemEntityManager &items() { return m_state.entities().items(); }
    /*! Returns the growing plants. */
    QcPlantManager &plants() { return m_state.plants(); }
    /*! Returns the state that belongs to single blocks (decision E-357). */
    QcBlockEntities &blockEntities() { return m_state.blockEntities(); }
    /*! Returns the rules of machines. */
    const QcMachineRules &machineRules() const { return m_state.machineRules(); }
    /*! Returns the flow of liquids. */
    QcLiquidFlow &liquids() { return m_state.liquids(); }
    /*! Returns the table of the falling blocks. */
    QcFallingBlockManager &fallingBlocks() { return m_state.entities().fallingBlocks(); }
    /*! Returns the queue of the burning fires (decision E-392). */
    QcFireQueue &fires() { return m_state.world().fires(); }
    /*! Returns the gameplay systems: fire, burning, health, weather, minecarts, experience,
        projectiles and circuits. */
    QcServerSystems &systems() { return m_systems; }
    /*! Returns the table of the armour stands. */
    QcArmourStandManager &stands() { return m_state.entities().stands(); }
    /*! Returns the table of the minecarts. */
    QcMinecartManager &carts() { return m_state.entities().minecarts(); }
    /*! Returns the table of the experience orbs (decision E-398). */
    QcExperienceOrbManager &orbs() { return m_state.entities().orbs(); }
    /*! Returns the state of the Signal Dust circuits. */
    QcCircuitState &circuitState() { return m_state.circuits(); }
    /*! Returns the queue of the Signal Hoppers. */
    QcHopperQueue &hopperQueue() { return m_state.hoppers(); }
    /*! The contents of the container or machine at \a pos changed (set by a test or tool):
        comparators reading it and hoppers next to it look again. */
    void noteContents(QcBlockPos pos) { m_state.noteContents(pos); }
    /*! Returns the blocks a piston slides. */
    const QcMovingBlocks &movingBlocks() const { return m_state.entities().movingBlocks(); }
    /*! Returns the game rule keepInventory of the world. */
    bool keepInventory() const { return m_state.info().keepInventory; }
    /*! Sets the game rule keepInventory of the world to \a keep. */
    void setKeepInventory(bool keep) { m_state.info().keepInventory = keep; }
    /*! Returns the seconds since the world was created (the day/night cycle). */
    double worldTime() const { return m_state.info().worldTime; }
    /*! Returns the problems found while opening the world (missing mods, unknown blocks); it
        still opened. */
    const QStringList &openWarnings() const { return m_persistence.openWarnings(); }
    /*! Returns whether openWorld() failed because another server has the world open. */
    bool worldInUse() const { return m_persistence.worldInUse(); }
    /*! Returns whether openWorld() failed because the world comes from an older development
        version. */
    bool worldTooOld() const { return m_persistence.worldTooOld(); }
    /*! Saves in the background like an autosave (tests and measurements). */
    void saveInBackground() { m_persistence.saveInBackground(); }
    /*! Returns which chunks are loaded and how they come and go (decisions E-374 to E-377). */
    QcChunkLoading &chunkLoading() { return m_chunkLoading; }
    /*! Waits until everything handed to the world's storage is written (autosaves run in the
        background, decision E-373). */
    void waitForStorage() { m_persistence.waitForStorage(); }
    /*! Returns whether openWorld() failed on a damaged per-region file, and which. */
    const QcServerPersistence::DamagedFile &damagedFile() const { return m_persistence.damagedFile(); }
    /*! Sets the world time to \a seconds (at least 0) and tells every playing client. */
    void setWorldTime(double seconds);

    /*! The first slot of the crafting grid in inventory moves (QcMsg::MoveInventoryItem). */
    static constexpr int craftingGridSlot = QcServerInventory::craftingGridSlot;
    /*! Returns the configuration. */
    const QcServerConfig &config() const { return m_state.config(); }

    /*! Runs one simulation tick immediately (used by tests and by the tick timer). */
    void tick();
    /*! Returns the autosaves since the server started. */
    quint64 autosaveCount() const { return m_saving.autosaveCount(); }
    /*! Returns whether the tick loop pauses (decision E-877): tick() runs only the players'
        upkeep then. */
    const QcServerPause &pause() const { return m_pause; }
    /*! Returns the server ticks per tick of a client's clock, sent with WorldTime: the tick rate
        / 20, during a sprint the larger of sprint and tick rate / 20, at most
        QcMsg::WorldTime::maxSpeed; 0 while frozen or paused (decisions E-249, E-877). */
    double timeSpeed() const;

    /*! Neighbour updates per tick at most by default (QcServerLimits::blockUpdatesPerTick). */
    static constexpr int maxBlockUpdatesPerTick = QcBlockSimulation::maxBlockUpdatesPerTick;
    /*! Returns the queues of block updates and their counters (pending, waiting, processed,
        dropped). */
    const QcBlockUpdateQueue &blockUpdates() const { return m_blockUpdates; }

    /*! Returns the duration and lateness of the timed ticks; safe to read and reset from other
        threads (benchmark). */
    QcTickStats::Summary tickTimes() const { return m_ticks.stats().summary(); }
    /*! Forgets the tick times recorded so far; safe from other threads (benchmark). */
    void resetTickTimes() { m_ticks.stats().reset(); }
    /*! Returns the players, loaded chunks, creatures and items, the network traffic of the
        clients and the tick durations since the previous call for the health line (decision
        E-834); server thread only. */
    [[nodiscard]] QcHealthSample health();
    /*! Returns the time per phase of the latest ticks (decision E-225); server thread only. */
    const QcTickPhaseTimer &tickPhases() const { return m_phases; }
    /*! Returns the tick schedule: rate and sprints (/tick, decision E-249); server thread only. */
    QcTickScheduler &tickScheduler() { return m_ticks; }
    /*! Returns whether the world is frozen and the steps left (/tick); server thread only. */
    const QcTickControl &tickControl() const { return m_tickControl; }
    /*! Returns the systems of the tick in their order (decision E-700); server thread only. */
    [[nodiscard]] const QcTickSystems &tickSystems() const { return m_tickSystems; }

    /*! Changes the login settings for network players to \a login (a world opened to the LAN,
        decision E-252). */
    void setLoginSettings(const QcLoginSettings &login);
    /*! Ends the connections of all network clients with a reason (the host stopped sharing the
        world); the local player stays. They are saved with the next tick like any who leave. */
    void disconnectRemoteClients();
    /*! Runs \a line of the server console (decision E-247); the replies go to \a output. */
    void runConsoleCommand(const QString &line, const QcCommandSource::Output &output);
    /*! Returns the command layer (chat, console, admin channel). */
    QcServerCommands &commands() { return m_commands; }

signals:
    /*! The number of clients changed to \a count. */
    void clientCountChanged(int count);
    /*! /stop from the console: the host should stop the server. */
    void stopRequested();

private:
    using Client = QcServerClient;

    void handleMessage(Client &client, const QcMessage &message);
    // Pays the message's token (decision E-091); false when it is dropped.
    bool withinRateLimit(Client &client, const QcMessage &message);
    // Sends the true block of a dropped StartMining or FinishMining back, so a client that predicted
    // the break does not keep a phantom gap; once per position and tick, a few positions per tick.
    void resendDroppedMiningTarget(Client &client, const QcMessage &message);
    // Messages about moving, mining, using and fighting, chat and leaving; false for other types.
    bool handlePlayerMessage(Client &client, const QcMessage &message);
    // Messages about the inventory, crafting, machine screens and the look; false for other types.
    bool handleInventoryMessage(Client &client, const QcMessage &message);
    // The use button with something to consume in the hand: an empty map is used like food is eaten
    // (decision E-804), anything else eaten or drunk.
    void consumeHeld(Client &client);
    // Messages before the client plays: Hello, the login response and leaving; anything else ends
    // the connection.
    void handleLoginMessage(Client &client, const QcMessage &message);
    // Everyone on the server to every player (QcMsg::PlayerList), after joins, leaves and changes
    // of operator rights, and with the round trips the clients reported (decision E-886).
    void sendPlayerList();
    void handleInput(Client &client, const QcMsg::PlayerInput &message);
    // Replaces unknown outfits and hair styles with defaults; names come from the content.
    void handleSetAppearance(Client &client, const QcAppearance &appearance);
    void sendTime(Client &client);
    void sendTimeToAll();
    // Tells the operators of chunks found damaged (decision E-373).
    void reportDamagedChunks();
    // Add the systems of the tick in their order (decision E-700), once, from the constructor: the
    // players, saving and chunks, the simulation of the world, the updates of the clients.
    void addUpkeepSystems();
    void addSimulationSystems();
    void addClientSystems();
    void refillInputBudget(Client &client) const;
    // Input budget, the world and time as due, food and the player's own state for one client.
    void updateClient(Client &client, QcClientSyncPace::Due sync);
    // Timer slot: runs the due ticks and plans the next as a single shot (decision E-090).
    void runDueTicks();
    // Waits for the next tick when it is due within timerLeadNs (the timer fires that early).
    void waitUntilDue() const;
    // Arms the single shot timerLeadNs before the next tick is due, `remainingNs` from now.
    void armTickTimer(qint64 remainingNs);
    void removeClosedClients();
    // Pauses or resumes the tick loop as QcServerPause decides, saving before an empty pause and
    // telling the clients (decision E-877).
    void updatePause();
    // What a paused tick still does: logins time out, clients that left go (decision E-877).
    void pausedUpkeep();

    QcServerState m_state;
    QcTickScheduler m_ticks;
    QcTickPhaseTimer m_phases;
    QcLagReport m_lag;
    // Tick durations since the previous health() (the shared tick statistics stay untouched).
    QcIntervalStats m_healthTicks;
    QcTickControl m_tickControl;
    QcServerPause m_pause;
    // The systems of one tick in their order (decision E-700).
    QcTickSystems m_tickSystems;
    QcClientSyncPace m_syncPace;
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
    QcServerKitchen m_kitchen;
    QcServerWorkstations m_workstations;
    QcServerExplosions m_explosions;
    // Before the game modes, which take a spectator out of his cart.
    // NOLINT(qc-member-access): a subsystem of the server like the other value members (m_stands, m_combat, …), never replaced; no state of its own to guard.
    QcServerVehicles m_vehicles;
    QcServerGameModes m_gameModes;
    QcServerStands m_stands;
    QcServerAnimals m_animals;
    QcServerPlants m_plants;
    QcServerGolems m_golems;
    QcServerInteraction m_interaction;
    QcServerItems m_items;
    QcServerExperience m_experience;
    QcServerCombat m_combat;
    QcServerCreatures m_creatures;
    QcServerProjectiles m_projectiles;
    QcServerTrading m_trading;
    QcEnvironmentDamage m_environment;
    QcBodyBurning m_burning;
    QcServerEffects m_effects;
    QcLightningStrike m_lightning;
    QcSnowfall m_snowfall;
    QcEntitySync m_entitySync;
    QcChunkLoading m_chunkLoading;
    QcChunkStreaming m_chunkStreaming;
    QcServerPersistence m_persistence;
    // NOLINT(qc-member-access): a subsystem object, never replaced; the subsystem members wait for the class split.
    QcSaveSchedule m_saving;
    QcServerCommands m_commands;
    QcPlayerJoin m_join;
    QcServerLogin m_login;
    // The admin channel (decision E-848), also the network bytes of the clients that left (health()).
    QcServerAdmin m_admin;
    // Single shot shortly before the next due tick, in the same event loop as the messages of the
    // clients.
    QTimer *m_tickTimer = nullptr;
    QcServerDebug m_debug;
    QcServerSystems m_systems;
};
