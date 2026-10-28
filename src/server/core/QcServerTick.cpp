#include "server/core/QcServer.h"

#include "common/physics/QcPhysics.h"
#include "common/protocol/QcChatText.h"
#include "common/protocol/QcConnection.h"
#include "server/blockentities/QcServerSigns.h"
#include "server/core/QcServerLogging.h"
#include "server/hanging/QcServerHanging.h"
#include "server/maps/QcServerMaps.h"

#include <QTimer>

#include <algorithm>
#include <chrono>
#include <numeric>
#include <thread>

using namespace Qt::StringLiterals;

namespace {

// Logs a network client that is leaving, with its reason and how long it stayed.
void logLeaving(const QcServerClient &client)
{
    const QString error = client.connection ? client.connection->lastError() : QString();
    const QString why = error.isEmpty() ? u"disconnected"_s : error;
    const QString seconds = QString::number(static_cast<double>(client.login.connected.elapsed()) / 1000.0, 'f', 0);
    if (client.joined) {
        qCInfo(lcServer).noquote() << u"Player %1 (%2) left after %3 s: %4"_s.arg(client.name, client.login.address, seconds, why);
    } else {
        qCInfo(lcServer).noquote() << u"Connection from %1 (%2) closed before joining: %3"_s.arg(client.login.address, client.name, why);
    }
}

// Sends the BlockAck of the actions handled since the last world sync (decision E-853). After the
// sync: the blocks the actions set or resent went out when they were handled, the entities they
// spawned (drops) with the sync, so a confirmed action shows all of it.
void acknowledgeBlocks(QcServerClient &client)
{
    if (client.pendingBlockAck != 0) {
        client.connection->send(QcMsg::BlockAck{client.pendingBlockAck});
        client.pendingBlockAck = 0;
    }
}

} // namespace

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
    const qint64 untilDue = m_ticks.untilDueNs();
    if (untilDue <= 0 || untilDue > QcTickScheduler::timerLeadNs) {
        return;
    }

#ifdef Q_OS_WIN
    // Windows sleeps in whole milliseconds at best (QcTimerResolution) and often one more: sleep
    // 1 ms at a time while more than 1.5 ms remain, then wait the rest out actively (decision E-901).
    constexpr qint64 activeWaitNs = 1'500'000;
    while (m_ticks.untilDueNs() > activeWaitNs) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
#else
    constexpr qint64 activeWaitNs = 200'000;
    if (untilDue > activeWaitNs) {
        std::this_thread::sleep_for(std::chrono::nanoseconds(untilDue - activeWaitNs));
    }
#endif
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
    // A paused server runs no tick: the counter, the clock and the world stand still (decision E-877).
    if (m_pause.paused()) {
        m_ticks.skipCurrentTick();
        pausedUpkeep();
        return;
    }

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
    const QcTickPhaseTimer::Durations &phases = m_phases.lastTick();
    const double tickMs = static_cast<double>(std::accumulate(phases.begin(), phases.begin() + QcTickPhases::inTick, qint64{0})) / 1e6;
    m_healthTicks.add(tickMs);
    m_admin.noteTick(tickMs);
    if (const std::optional<QString> lag = m_lag.check(phases, m_phases.now())) {
        qCInfo(lcLag).noquote() << *lag;
    }
    // The server became empty long enough, or the last network player left the host's paused menu.
    updatePause();
}

void QcServer::pausedUpkeep()
{
    m_login.expire();
    removeClosedClients();
    m_commands.pollBackups();
    if (m_state.takePlayerListChanged()) {
        sendPlayerList();
    }
    // Console commands while the empty server rests are saved now; they reach the clients' view with
    // the first tick after the resume (decision E-877).
    if (m_pause.takeChange() && m_persistence.hasStorage()) {
        m_persistence.saveInBackground();
    }
    updatePause();
}

void QcServer::updatePause()
{
    const std::optional<QcServerPause::Reason> changed = m_pause.update(
        m_state.clients(), m_state.clock().playerTimeNs(), [this](const Client &client) { return m_chunkStreaming.viewComplete(client); });
    if (!changed) {
        return;
    }

    // An empty server saves before it rests, like an autosave; one save runs at a time.
    if (*changed == QcServerPause::Reason::Empty && m_persistence.hasStorage() && !m_persistence.saveRunning()) {
        m_lastAutosaveNs = m_state.clock().playerTimeNs();
        ++m_autosaves;
        m_persistence.saveInBackground();
    }
    if (m_pause.paused()) {
        qCInfo(lcServer).noquote() << u"Server paused (%1): no ticks until it resumes"_s.arg(QString::fromLatin1(QcServerPause::name(*changed)));
    } else {
        qCInfo(lcServer) << "Server resumed";
    }

    for (const auto &client : m_state.clients()) {
        if (client->state == Client::State::Playing) {
            client->connection->send(QcMsg::ServerPause{m_pause.paused()});
        }
    }
    // The clients' clocks of the day stand still with the server's (speed 0).
    sendTimeToAll();
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
        QcServerHanging(m_state, m_sounds, m_inventory).tick();
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
    // Boats move in the phase of the minecarts (decision E-920).
    m_tickSystems.add(Minecarts, simulating, [this] { m_vehicles.tick(); });
    m_tickSystems.add(Machines, simulating, [this] {
        m_machines.tick();
        m_kitchen.tick();
        m_screens.update();
        m_trading.update();
        m_workstations.update();
    });
}

void QcServer::addClientSystems()
{
    using enum QcTickPhase;
    using When = QcTickSystems::When;
    m_tickSystems.add(ClientSync, When::Always, [this] { m_chunkLoading.noteSimulated(); });
    // Held maps are revealed and their changes sent (decisions E-800, E-802).
    m_tickSystems.add(ClientSync, When::Simulating, [this] { QcServerMaps(m_state, m_inventory).tick(); });
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

    // The admin channel's status, chat and log for the operators who follow them (decision E-848).
    m_tickSystems.add(ClientSync, When::Always, [this] { m_admin.update(); });

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
    const qint64 intervalNs = static_cast<qint64>(m_state.config().autosaveSeconds) * 1'000'000'000;
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
    const double share = static_cast<double>(m_state.clock().lastTickNs()) / static_cast<double>(QcTickScheduler::intervalNs);
    const double cap = std::max(static_cast<double>(Client::maxInputBudget), share + Client::inputMargin);
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
        acknowledgeBlocks(client);
    }

    if (sync.time) {
        sendTime(client);
    }

    if (client.hunger.food() != client.sentFood) {
        m_health.sendFood(client);
    }

    if (client.stateDirty) {
        client.connection->send(QcMsg::PlayerState{client.input.lastSequence, client.motion, m_vehicles.vehicleOf(client), m_vehicles.boatOf(client)});
        client.stateDirty = false;
    }
}

QcHealthSample QcServer::health()
{
    QcHealthSample sample;
    sample.players = std::ranges::count_if(m_state.clients(), [](const auto &client) { return client->joined; });
    sample.loadedChunks = m_state.world().loadedChunkCount();
    sample.entities = m_state.entities().creatures().mobs().size() + m_state.entities().items().items().size();
    // Bytes of the clients that left count on, so the rates never go back.
    sample.traffic = m_admin.traffic();
    sample.ticks = m_healthTicks.take();
    sample.paused = m_pause.paused();
    return sample;
}

void QcServer::removeClosedClients()
{
    bool leaving = false;
    for (const auto &client : m_state.clients()) {
        if (client->state == Client::State::Closed && !client->local) {
            logLeaving(*client);
        }
        if (client->state == Client::State::Closed) {
            m_admin.noteLeft(*client);
        }
        if (client->state == Client::State::Closed && client->joined) {
            // Only a player who was on the list changes it; refused logins do not.
            m_state.notePlayerListChanged();
            m_crafting.returnCraftingGrid(*client);
            m_workstations.leave(*client);
            // A rider gets out first, so he is saved standing beside the cart or the boat.
            m_vehicles.leave(*client);
            m_commands.playerLeft(client->id);
            QcServerMaps(m_state, m_inventory).forget(*client);
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
