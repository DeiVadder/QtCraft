#include "server/core/QcServer.h"

#include "common/physics/QcPhysics.h"
#include "common/protocol/QcChatText.h"
#include "common/protocol/QcConnection.h"
#include "server/blockentities/QcServerSigns.h"
#include "server/core/QcServerLogging.h"

#include <QTimer>

#include <algorithm>
#include <chrono>
#include <thread>

void QcServer::runDueTicks()
{
    waitUntilDue();
    armTickTimer(m_ticks.runDue([this] { tick(); }));
    m_commands.afterTicks();
}

void QcServer::waitUntilDue() const
{
    // The timer fired up to timerLeadNs early. A sleep ends 0.1-0.5 ms late, the event loop's
    // timers 1-3 ms; the last fraction of a millisecond is waited out actively.
    constexpr qint64 activeWaitNs = 200'000;
    const qint64 untilDue = m_ticks.untilDueNs();
    if (untilDue <= 0 || untilDue > QcTickScheduler::timerLeadNs) {
        return;
    }

    if (untilDue > activeWaitNs) {
        std::this_thread::sleep_for(std::chrono::nanoseconds(untilDue - activeWaitNs));
    }
    while (m_ticks.untilDueNs() > 0) {
        std::this_thread::yield();
    }
}

void QcServer::armTickTimer(qint64 remainingNs)
{
    m_tickTimer->start(std::chrono::milliseconds(std::max<qint64>(0, remainingNs - QcTickScheduler::timerLeadNs) / 1'000'000));
}

void QcServer::tick()
{
    // Each system's lap gives the time since the previous one to its phase (decisions E-225, E-700);
    // the start of the tick counts to the first phase.
    m_phases.beginTick();
    m_state.advanceClock(m_ticks.tickRealNs());
    m_chunkLoading.beginTick();
    // A frozen world only ticks for the players (decision E-249).
    const bool simulating = m_tickControl.simulateNext();
    if (simulating && m_state.info().daylightCycle) {
        m_state.info().worldTime += QcPhysics::tickDuration;
    }
    m_state.entities().creatures().setNaturalSpawning(m_state.info().mobSpawning);
    m_state.entities().creatures().setHurtTicks(m_state.info().mobInvulnerabilityTicks);

    m_tickSystems.run(simulating, m_phases);
    m_phases.endTick(m_ticks.currentLateNs());
    m_ticks.stats().addPhases(m_phases.lastTick());
}

void QcServer::addUpkeepSystems()
{
    using enum QcTickPhase;
    using When = QcTickSystems::When;
    m_tickSystems.add(Players, When::Always, [this] {
        m_login.expire();
        flushIdentitiesWhenDue();
        removeClosedClients();
        m_commands.pollBackups();
        if (m_state.takePlayerListChanged()) {
            sendPlayerList();
        }
    });
    m_tickSystems.add(Saving, When::Always, [this] {
        autosave();
        reportDamagedChunks();
    });

    // Entities in chunks that are not loaded rest for this tick, every kind by the same rule; also in
    // a frozen world, whose clients still get what comes into view (E-249). Their chunks load, so
    // they go back to storage with them (decision E-374).
    m_tickSystems.add(ChunkLoading, When::Always, [this] {
        m_chunkLoading.admitArrivals();
        m_blockUpdates.handleLoadedChunks();
        m_chunkLoading.requestStrays(
            m_state.entities().settle([this](QcChunkPos chunk) { return m_state.world().chunk(chunk) != nullptr; }, m_state.world().loadedEpoch()));
    });
}

void QcServer::addSimulationSystems()
{
    using enum QcTickPhase;
    constexpr QcTickSystems::When simulating = QcTickSystems::When::Simulating;
    // Contents spilled by blocks set outside a block change of the server (the spawn's ground, tests).
    m_tickSystems.add(BlockUpdates, simulating, [this] {
        m_blocks.dropSpills();
        m_blockSimulation.processBlockUpdates();
    });
    m_tickSystems.add(Signal, simulating, [this] { m_circuits.tick(); });
    m_tickSystems.add(Liquids, simulating, [this] { m_blockSimulation.processLiquids(); });
    m_tickSystems.add(Fire, simulating, [this] { m_fire.tick(); });
    m_tickSystems.add(Weather, simulating, [this] {
        m_weather.tick();
        m_snowfall.tick();
    });
    m_tickSystems.add(Mobs, simulating, [this] {
        m_health.tick();
        m_interaction.swingWhileMining();
        m_stands.tick();
        m_creatures.tick();
        m_environment.applyContactDamage();
        m_burning.tick();
        m_effects.tick();
        m_environment.applySuffocation();
    });
    m_tickSystems.add(Projectiles, simulating, [this] {
        m_projectiles.tick();
        m_creatures.announceTeleports();
    });
    m_tickSystems.add(Items, simulating, [this] {
        m_plants.plantItems(m_items.tick());
        m_experience.tick();
    });
    m_tickSystems.add(Plants, simulating, [this] {
        m_plants.tick();
        m_animals.tick();
    });
    m_tickSystems.add(FallingBlocks, simulating, [this] { m_fallingBlocks.tick(); });
    m_tickSystems.add(Explosions, simulating, [this] { m_explosions.tick(); });
    m_tickSystems.add(Minecarts, simulating, [this] { m_minecarts.tick(); });
    m_tickSystems.add(Machines, simulating, [this] {
        m_machines.tick();
        m_kitchen.tick();
        m_screens.update();
        m_trading.update();
    });
}

void QcServer::addClientSystems()
{
    using enum QcTickPhase;
    using When = QcTickSystems::When;
    m_tickSystems.add(ClientSync, When::Always, [this] { m_chunkLoading.noteSimulated(); });
    // A step of a frozen world moves the time the clients do not advance themselves.
    m_tickSystems.add(ClientSync, When::Simulating, [this] {
        if (m_tickControl.frozen()) {
            sendTimeToAll();
        }
    });
    m_tickSystems.add(ClientSync, When::Always, [this] {
        // Sign texts written since the last tick, also in a frozen world (decision E-766).
        QcServerSigns(m_state).announceChanges();
        const QcClientSyncPace::Due sync = m_syncPace.next(m_state.clock(), m_ticks.sprinting());
        for (const auto &client : m_state.clients()) {
            if (client->state == Client::State::Playing) {
                updateClient(*client, sync);
            }
        }

        for (const auto &client : m_state.clients()) {
            client->swung = false;
            client->hurtThisTick = false;
        }
        m_state.entities().forgetDeaths();
    });

    // Chunks no one needs any more leave the world, whole groups at a time (decision E-374).
    m_tickSystems.add(Streaming, When::Always, [this] {
        m_chunkLoading.updateTickets();
        m_chunkLoading.unloadUnneeded();
    });
}

void QcServer::reportDamagedChunks()
{
    const QList<QcChunkPos> damaged = m_state.world().saves().takeNewlyQuarantined();
    for (const QcChunkPos &chunk : damaged) {
        const QString x = QString::number(chunk.x * QcWorld::chunkSize);
        const QString z = QString::number(chunk.z * QcWorld::chunkSize);
        for (const auto &client : m_state.clients()) {
            if (m_state.permissionOf(*client) == QcPermission::Operator) {
                client->connection->send(QcChatText::message(
                    QT_TRANSLATE_NOOP("QcServerMessages",
                                      "The saved data of the chunk at %1 %2 is damaged: it plays as generated and its changes are not saved. "
                                      "/regenerate there gives the data up."),
                    {x, z}));
            }
        }
    }
}

void QcServer::autosave()
{
    // A failed save in the background is told to everyone once per streak of failures (review of 31.3).
    if (const std::optional<QcStatus> failed = m_persistence.takeNewFailure()) {
        qCWarning(lcServer) << "Autosave failed:" << failed->error();
        for (const auto &client : m_state.clients()) {
            client->connection->send(QcChatText::message(QT_TRANSLATE_NOOP("QcServerMessages", "Autosave failed: %1"), {failed->error()}));
        }
    }

    // In the players' time: neither a high tick rate nor a sprint saves more often (decision E-249).
    // Unloaded chunks waiting in memory beyond their limit are written early (decision E-374), unless
    // saving fails: then only the normal interval tries again. One save runs at a time.
    const qint64 intervalNs = qint64(m_state.config().autosaveSeconds) * 1'000'000'000;
    const bool due = intervalNs > 0 && m_state.clock().playerTimeNs() - m_lastAutosaveNs >= intervalNs;
    const bool early = m_persistence.manyUnsavedChunks() && !m_persistence.savesFailing();
    if (!m_persistence.hasStorage() || m_persistence.saveRunning() || (!due && !early)) {
        return;
    }

    m_lastAutosaveNs = m_state.clock().playerTimeNs();
    ++m_autosaves;
    m_persistence.saveInBackground();
}

void QcServer::refillInputBudget(Client &client) const
{
    // One input per 50 ms of the real time the tick stands for, whatever the tick rate; the cap
    // holds a whole tick's share, so slow rates keep the player's pace (decision E-249).
    const double share = double(m_state.clock().lastTickNs()) / double(QcTickScheduler::intervalNs);
    const double cap = std::max(double(Client::maxInputBudget), share + Client::inputMargin);
    client.input.budget = std::min(client.input.budget + share, cap);
}

void QcServer::updateClient(Client &client, QcClientSyncPace::Due sync)
{
    refillInputBudget(client);
    m_phases.lap(QcTickPhase::ClientSync);
    if (sync.world) {
        m_chunkStreaming.stream(client);
        m_phases.lap(QcTickPhase::Streaming);
        m_entitySync.sync(client);
        m_debug.update(client);
    }

    if (sync.time) {
        sendTime(client);
    }

    if (client.hunger.food() != client.sentFood) {
        m_health.sendFood(client);
    }

    if (client.stateDirty) {
        client.connection->send(QcMsg::PlayerState{client.input.lastSequence, client.motion, m_minecarts.vehicleOf(client)});
        client.stateDirty = false;
    }
}

void QcServer::removeClosedClients()
{
    bool leaving = false;
    for (const auto &client : m_state.clients()) {
        if (client->state == Client::State::Closed && client->joined) {
            // Only a player who was on the list changes it; refused logins do not.
            m_state.notePlayerListChanged();
            m_crafting.returnCraftingGrid(*client);
            // A rider gets out first, so he is saved standing beside the cart.
            m_minecarts.leave(*client);
            m_commands.playerLeft(client->id);
            leaving = true;
        }
    }
    // A player who leaves is saved with the world, in the background (decision E-378); failures go
    // to the log and to the others with the next autosave report.
    if (leaving) {
        m_persistence.saveInBackground();
    }

    const auto removed = std::erase_if(m_state.clientList(), [](const std::unique_ptr<Client> &client) {
        return client->state == Client::State::Closed;
    });

    if (removed > 0) {
        emit clientCountChanged(clientCount());
    }
}
