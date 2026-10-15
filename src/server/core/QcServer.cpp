#include "QcServer.h"

#include "QcChatText.h"
#include "QcConnection.h"
#include "QcDayCycle.h"
#include "QcLineOfSight.h"
#include "QcPhysics.h"
#include "QcProfileValidation.h"
#include "QcProtocol.h"
#include "QcServerSetup.h"
#include "QcTransport.h"

#include <QLoggingCategory>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

using namespace Qt::StringLiterals;

namespace {

Q_LOGGING_CATEGORY(lcServer, "qtcraft.server")

} // namespace

QcServer::QcServer(QcServerConfig config, std::shared_ptr<const QcContentRegistry> content,
                   std::unique_ptr<QcWorldGenerator> generator, QObject *parent)
    : QObject(parent)
    , m_state(std::move(config), std::move(content), std::move(generator))
    , m_sounds(m_state, m_ticks)
    , m_blockUpdates(m_state)
    , m_blocks(m_state, m_sounds, m_blockUpdates)
    , m_fallingBlocks(m_state, m_sounds, m_blockUpdates, m_blocks)
    , m_fire(m_state, m_blocks, m_sounds)
    , m_blockSimulation(m_state, m_sounds, m_blockUpdates, m_blocks, m_fallingBlocks)
    , m_circuits(m_state, m_sounds, m_blocks)
    , m_screens(m_state)
    , m_machines(m_state, m_sounds)
    , m_inventory(m_state, m_screens, m_sounds)
    , m_crafting(m_state, m_inventory)
    , m_ignition(m_state, m_fire, m_sounds, m_inventory)
    , m_health(m_state, m_sounds, m_inventory)
    , m_gameModes(m_state, m_inventory, m_crafting)
    , m_stands(m_state, m_sounds, m_inventory)
    , m_minecarts(m_state, m_sounds, m_inventory, m_screens)
    , m_animals(m_state, m_sounds, m_inventory)
    , m_plants(m_state, m_sounds, m_blocks, m_inventory)
    , m_interaction(m_state, {m_sounds, m_blocks, m_inventory, m_crafting, m_screens, m_stands, m_minecarts, m_animals, m_plants, m_health, m_circuits, m_ignition})
    , m_items(m_state, m_sounds, m_inventory)
    , m_combat(m_state, m_sounds, m_inventory)
    , m_creatures(m_state, m_sounds, m_health)
    , m_environment(m_state, m_sounds, m_health)
    , m_burning(m_state, m_sounds, m_health)
    , m_entitySync(m_state)
    , m_chunkLoading(m_state, m_blocks, m_blockUpdates)
    , m_chunkStreaming(m_state, m_chunkLoading)
    , m_persistence(m_state, m_blocks, m_blockUpdates, m_chunkLoading)
    , m_commands(m_state, {m_blocks, m_blockUpdates,
                           {m_inventory, m_health, m_stands, m_gameModes, m_minecarts, [this](double seconds) { setWorldTime(seconds); }},
                           {m_phases, m_ticks, [this] { return save(); }, [this] { emit stopRequested(); }},
                           {m_ticks, m_tickControl, m_phases, [this] { sendTimeToAll(); }}})
    , m_join(m_state, {m_persistence, m_inventory, m_health, m_gameModes, m_chunkStreaming, [this](Client &client) { sendTime(client); }})
    , m_login(m_state, [this](Client &client) { m_join.join(client); })
    , m_tickTimer(new QTimer(this))
    , m_debug(m_state, m_blockUpdates, m_chunkLoading, m_phases, m_ticks)
{
    static_assert(QcTickScheduler::intervalNs == qint64(QcPhysics::tickDuration * 1e9));
    m_tickTimer->setTimerType(Qt::PreciseTimer);
    m_tickTimer->setSingleShot(true);
    connect(m_tickTimer, &QTimer::timeout, this, &QcServer::runDueTicks);
    m_circuits.setIgnite([this](QcBlockPos pos) { return m_ignition.strike(pos); });
    m_blockSimulation.setIgnition(&m_ignition);
    m_circuits.setEquipPlayer([this](QcServerClient &client, const QcEquipment &equipment) { m_inventory.equipmentWorn(client, equipment, false); });
}

QcServer::~QcServer()
{
    // A server destroyed without stop() (tests, a failed start) keeps its registrations too.
    flushIdentities();
}

void QcServer::start()
{
    m_ticks.start();
    armTickTimer(QcTickScheduler::intervalNs);
}

QcStatus QcServer::openWorld()
{
    if (const QcStatus opened = m_persistence.open(); !opened) {
        return opened;
    }

    QStringList accessWarnings;
    m_state.access().open(m_state.config().worldDirectory, m_state.config().operators, accessWarnings);
    m_state.identities().open(m_state.config().worldDirectory, accessWarnings);
    m_persistence.noteWarnings(accessWarnings);

    // A new world gets what its generator sets up once (decision E-221).
    if (!m_state.info().setupDone) {
        QcServerSetup::apply(m_state, m_blockUpdates, m_state.world().generator().setup());
    }
    return {};
}

QcStatus QcServer::save()
{
    flushIdentities();
    return m_persistence.save();
}

void QcServer::flushIdentitiesWhenDue()
{
    // New identities are written at most once a second, not with every login (decision E-273).
    if (m_state.identities().hasUnsavedChanges() && m_state.clock().playerTimeNs() - m_lastIdentityFlushNs >= identityFlushIntervalNs) {
        flushIdentities();
    }
}

void QcServer::flushIdentities()
{
    m_lastIdentityFlushNs = m_state.clock().playerTimeNs();
    // Failures are logged by the store; the registrations count until the restart.
    [[maybe_unused]] const QcStatus flushed = m_state.identities().flush();
}

QcStatus QcServer::stop()
{
    m_tickTimer->stop();

    // Open crafting grids are emptied like on a disconnect, so overflow lands in the saved world.
    for (const auto &client : m_state.clients()) {
        // Also clients that already disconnected: removeClosedClients() will not run any more.
        if (client->joined) {
            m_crafting.returnCraftingGrid(*client);
        }
    }

    const QcStatus saved = save();
    if (!saved) {
        qCCritical(lcServer) << "Saving failed:" << saved.error();
    }
    // A backup being copied finishes first; it reads only its snapshot, not the world.
    m_commands.waitForBackups();

    for (const auto &client : m_state.clients()) {
        client->connection->send(QcChatText::disconnect(QT_TRANSLATE_NOOP("QcServerMessages", "The server stopped")));
        client->connection->close();
    }
    return saved;
}

void QcServer::addClient(std::unique_ptr<QcTransport> transport, ClientOrigin origin)
{
    auto client = std::make_unique<Client>();
    client->local = origin == ClientOrigin::Local;
    client->login.address = transport->peerAddress();
    client->login.connected.start();
    Client *raw = client.get();
    client->connection = std::make_unique<QcConnection>(std::move(transport), QcProtocol::Peer::Client,
                                                        [this, raw](QcMessage &&message) { handleMessage(*raw, std::move(message)); });
    connect(client->connection.get(), &QcConnection::closed, this, [raw] { raw->state = Client::State::Closed; });

    m_state.clientList().push_back(std::move(client));
    emit clientCountChanged(clientCount());
}

void QcServer::handleMessage(Client &client, QcMessage &&message)
{
    const QcTickPhaseTimer::MessageScope timed(m_phases);
    if (client.state == Client::State::Closed) {
        return;
    }

    if (client.state != Client::State::Playing) {
        handleLoginMessage(client, message);
        return;
    }

    // Login messages are exempt from the rate limit: once is all a player may send them (E-254).
    if (std::holds_alternative<QcMsg::Hello>(message) || std::holds_alternative<QcMsg::LoginResponse>(message)) {
        QcServerLogin::refuse(client, QcChatText::disconnect(QT_TRANSLATE_NOOP("QcServerMessages", "Unexpected login message")));
        return;
    }

    if (!withinRateLimit(client, message)) {
        return;
    }

    // The dead neither mine nor use items; their mining state is dropped.
    const bool worldAction = std::holds_alternative<QcMsg::StartMining>(message)
                             || std::holds_alternative<QcMsg::FinishMining>(message)
                             || std::holds_alternative<QcMsg::UseItem>(message) || std::holds_alternative<QcMsg::UseEntity>(message);
    if (client.dead && worldAction) {
        client.mining.pos.reset();
        return;
    }

    if (!handlePlayerMessage(client, message) && !handleInventoryMessage(client, message)) {
        qCWarning(lcServer) << "Ignoring unexpected message type" << message.index() << "from" << client.name;
    }
}

bool QcServer::withinRateLimit(Client &client, const QcMessage &message)
{
    const std::optional<QcRateLimiter::Kind> kind = QcRateLimiter::kindOf(message);
    // The local player (in-process host, the operator) is not limited in chat: command bursts of
    // scripts and benchmark scenarios must arrive complete. All other kinds stay limited for him.
    const bool exempt = kind && client.local && *kind == QcRateLimiter::Kind::Chat;
    if (!kind || exempt || client.input.rateLimiter.allow(*kind, m_state.clock().playerTimeNs())) {
        return true;
    }

    if (*kind == QcRateLimiter::Kind::Mining) {
        resendDroppedMiningTarget(client, message);
    }

    // Dropped without a kick; the log names the spammer at most once a second per kind.
    if (const quint64 dropped = client.input.rateLimiter.takeDropReport(*kind, m_state.clock().playerTimeNs()); dropped > 0) {
        const QLatin1StringView kindName(QcRateLimiter::name(*kind));
        qCWarning(lcServer).noquote() << u"Rate limit: dropped %1 %2 messages from %3"_s.arg(dropped).arg(kindName, client.name);
    }
    return false;
}

void QcServer::resendDroppedMiningTarget(Client &client, const QcMessage &message)
{
    std::optional<QcBlockPos> pos;
    if (const auto *start = std::get_if<QcMsg::StartMining>(&message)) {
        pos = start->pos;
    } else if (const auto *finish = std::get_if<QcMsg::FinishMining>(&message)) {
        pos = finish->pos;
    }
    if (!pos || !QcLineOfSight::isWithinReach(client, *pos)) {
        return;
    }

    // A flood must not turn into a flood of answers: each position once per tick, and only as many
    // positions as the real client mines in one tick.
    if (client.input.miningResendTick != m_state.clock().tick()) {
        client.input.miningResendTick = m_state.clock().tick();
        client.input.miningResends.clear();
    }
    if (client.input.miningResends.contains(*pos) || client.input.miningResends.size() >= Client::maxMiningResendsPerTick) {
        return;
    }

    if (const std::optional<QcBlockId> block = m_state.world().blockAt(*pos)) {
        client.input.miningResends.append(*pos);
        client.connection->send(QcMsg::BlockUpdate{*pos, *block});
    }
}

bool QcServer::handlePlayerMessage(Client &client, const QcMessage &message)
{
    if (const auto *input = std::get_if<QcMsg::PlayerInput>(&message)) {
        handleInput(client, *input);
    } else if (const auto *start = std::get_if<QcMsg::StartMining>(&message)) {
        client.mining.pos = start->pos;
        client.mining.startTick = m_state.clock().tick();
        client.mining.startNs = m_state.clock().playerTimeNs();
    } else if (std::holds_alternative<QcMsg::CancelMining>(message)) {
        client.mining.pos.reset();
    } else if (const auto *finish = std::get_if<QcMsg::FinishMining>(&message)) {
        m_interaction.finishMining(client, finish->pos);
    } else if (const auto *use = std::get_if<QcMsg::UseItem>(&message)) {
        m_interaction.useItem(client, *use);
    } else if (const auto *attack = std::get_if<QcMsg::AttackEntity>(&message)) {
        if (!m_stands.hit(client, attack->id) && !m_minecarts.hit(client, attack->id)) {
            m_combat.attack(client, attack->id);
        }
    } else if (const auto *useEntity = std::get_if<QcMsg::UseEntity>(&message)) {
        if (!m_stands.use(client, useEntity->id) && !m_minecarts.use(client, useEntity->id)) {
            m_interaction.useEntity(client, useEntity->id);
        }
    } else if (std::holds_alternative<QcMsg::Respawn>(message)) {
        m_health.respawn(client);
    } else if (const auto *chat = std::get_if<QcMsg::ChatInput>(&message)) {
        m_commands.chat(client, chat->text.left(256));
    } else if (std::holds_alternative<QcMsg::Disconnect>(message)) {
        client.connection->close();
    } else if (const auto *debug = std::get_if<QcMsg::DebugSubscribe>(&message)) {
        m_debug.subscribe(client, debug->flags);
    } else {
        return false;
    }

    return true;
}

bool QcServer::handleInventoryMessage(Client &client, const QcMessage &message)
{
    if (const auto *select = std::get_if<QcMsg::SelectHotbarSlot>(&message)) {
        client.inventory.setSelectedHotbarSlot(select->slot);
        client.mining.pos.reset();
        m_inventory.send(client);
    } else if (const auto *move = std::get_if<QcMsg::MoveInventoryItem>(&message)) {
        client.mining.pos.reset();
        m_inventory.moveItem(client, *move);
    } else if (const auto *quick = std::get_if<QcMsg::QuickMoveItem>(&message)) {
        client.mining.pos.reset();
        if (quick->slot == QcMsg::QuickMoveItem::craftingResultSlot) {
            m_crafting.craftAll(client);
        } else {
            m_inventory.quickMove(client, *quick);
        }
    } else if (std::holds_alternative<QcMsg::TakeCraftingResult>(message)) {
        m_crafting.takeCraftingResult(client);
    } else if (const auto *craft = std::get_if<QcMsg::CraftRecipe>(&message)) {
        m_crafting.craftRecipe(client, *craft);
    } else if (std::holds_alternative<QcMsg::CloseCrafting>(message)) {
        m_crafting.closeCrafting(client);
    } else if (std::holds_alternative<QcMsg::ConsumeItem>(message)) {
        m_health.consume(client);
    } else if (std::holds_alternative<QcMsg::CloseMachine>(message)) {
        m_screens.close(client, false);
    } else if (const auto *drop = std::get_if<QcMsg::DropItem>(&message)) {
        m_inventory.dropItem(client, *drop);
    } else if (const auto *look = std::get_if<QcMsg::SetAppearance>(&message)) {
        handleSetAppearance(client, look->appearance);
    } else if (const auto *creative = std::get_if<QcMsg::CreativeItem>(&message)) {
        m_gameModes.creativeItem(client, *creative);
    } else if (const auto *pick = std::get_if<QcMsg::PickBlock>(&message)) {
        m_gameModes.pickBlock(client, pick->pos);
    } else {
        return false;
    }

    return true;
}

void QcServer::handleLoginMessage(Client &client, const QcMessage &message)
{
    if (const auto *hello = std::get_if<QcMsg::Hello>(&message)) {
        m_login.hello(client, *hello);
    } else if (const auto *response = std::get_if<QcMsg::LoginResponse>(&message)) {
        m_login.response(client, *response);
    } else if (std::holds_alternative<QcMsg::Disconnect>(message)) {
        client.connection->close();
    } else {
        // Unauthenticated clients may only log in.
        QcServerLogin::refuse(client, QcChatText::disconnect(QT_TRANSLATE_NOOP("QcServerMessages", "Expected Hello")));
    }
}

void QcServer::sendPlayerList()
{
    QcMsg::PlayerList list;
    for (const auto &client : m_state.clients()) {
        if (client->state == Client::State::Playing) {
            list.players.append({client->id, client->name, m_state.permissionOf(*client) == QcPermission::Operator});
        }
    }

    for (const auto &client : m_state.clients()) {
        if (client->state == Client::State::Playing) {
            client->connection->send(list);
        }
    }
}

void QcServer::setLoginSettings(const QcLoginSettings &login)
{
    m_state.config().apply(login);
    m_login.forgetPasswordKey();
}

void QcServer::disconnectRemoteClients()
{
    for (const auto &client : m_state.clients()) {
        if (!client->local && client->state != Client::State::Closed) {
            client->connection->send(QcChatText::disconnect(QT_TRANSLATE_NOOP("QcServerMessages", "The host stopped sharing the world")));
            client->connection->close();
        }
    }
}

void QcServer::runConsoleCommand(const QString &line, const QcCommandSource::Output &output)
{
    m_commands.console(line, output);
}

void QcServer::handleInput(Client &client, const QcMsg::PlayerInput &message)
{
    if (client.input.budget < 1.0) {
        return;
    }

    if (client.dead) {
        // Acknowledge without moving so the client's queue of predicted inputs drains.
        client.input.lastSequence = message.input.sequence;
        client.stateDirty = true;
        return;
    }

    client.input.budget -= 1.0;

    // The decoder already rejects non-finite values; ranges are enforced here.
    QcPlayerInput input = message.input;
    input.forward = std::clamp(input.forward, -1.0f, 1.0f);
    input.strafe = std::clamp(input.strafe, -1.0f, 1.0f);
    input.pitch = std::clamp(input.pitch, -90.0f, 90.0f);
    input.yaw = std::fmod(input.yaw, 360.0f);

    // A rider does not move: the cart carries the player (decision E-382).
    if (m_minecarts.steer(client, input)) {
        client.input.lastSequence = input.sequence;
        client.stateDirty = true;
        return;
    }

    const bool wasOnGround = client.motion.onGround;
    const bool wasInWater = client.motion.inWater;
    const QcVec3 previous = client.motion.position;
    client.previousPosition = previous;
    QcPhysics::stepPlayer(client.motion, input, m_state.world(), QcPhysics::tickDuration, client.rules().mayFly);
    m_health.afterMove(client, wasOnGround, wasInWater, previous.y);

    client.yaw = input.yaw;
    client.pitch = input.pitch;
    // Sneak also moves a flying player down; it still counts for using blocks (placing against a
    // chest), only the figure does not crouch for it (QcEntitySync).
    client.sneaking = input.sneak;
    client.sprinting = input.sprint && input.forward > 0.0f && !input.sneak;

    const bool jumped = input.jump && wasOnGround && !client.motion.onGround && client.motion.velocity.y > 0.0;
    const double distance = std::hypot(client.motion.position.x - previous.x, client.motion.position.z - previous.z);
    client.hunger.exhaust(QcHunger::movementExhaustion(distance, client.sprinting, client.motion.inWater, jumped));

    client.input.lastSequence = input.sequence;
    client.stateDirty = true;
}

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
    // Each lap gives the time since the previous one to its phase (decision E-225).
    using Phase = QcTickPhase;
    m_phases.beginTick();
    m_state.advanceClock(m_ticks.tickRealNs());
    m_chunkLoading.beginTick();
    // A frozen world only ticks for the players (decision E-249).
    const bool simulating = m_tickControl.simulateNext();
    if (simulating && m_state.info().daylightCycle) {
        m_state.info().worldTime += QcPhysics::tickDuration;
    }
    m_state.entities().creatures().setNaturalSpawning(m_state.info().mobSpawning);

    m_login.expire();
    flushIdentitiesWhenDue();
    removeClosedClients();
    m_commands.pollBackups();
    if (m_state.takePlayerListChanged()) {
        sendPlayerList();
    }
    m_phases.lap(Phase::Players);
    autosave();
    reportDamagedChunks();
    m_phases.lap(Phase::Saving);
    m_chunkLoading.admitArrivals();
    m_blockUpdates.handleLoadedChunks();
    // Entities in chunks that are not loaded rest for this tick, every kind by the same rule; also in
    // a frozen world, whose clients still get what comes into view (E-249). Their chunks load, so
    // they go back to storage with them (decision E-374).
    m_chunkLoading.requestStrays(
        m_state.entities().settle([this](QcChunkPos chunk) { return m_state.world().chunk(chunk) != nullptr; }, m_state.world().loadedEpoch()));
    m_phases.lap(Phase::ChunkLoading);

    if (simulating) {
        simulate();
    }
    m_chunkLoading.noteSimulated();
    // A step of a frozen world moves the time the clients do not advance themselves.
    if (simulating && m_tickControl.frozen()) {
        sendTimeToAll();
    }

    const ClientSync sync = clientSyncDue();
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
    m_phases.lap(Phase::ClientSync);

    // Chunks no one needs any more leave the world, whole groups at a time (decision E-374).
    m_chunkLoading.updateTickets();
    m_chunkLoading.unloadUnneeded();
    m_phases.lap(Phase::Streaming);
    m_phases.endTick(m_ticks.currentLateNs());
    m_ticks.stats().addPhases(m_phases.lastTick());
}

void QcServer::simulate()
{
    using Phase = QcTickPhase;
    // Contents spilled by blocks set outside a block change of the server (the spawn's ground, tests).
    m_blocks.dropSpills();
    m_blockSimulation.processBlockUpdates();
    m_phases.lap(Phase::BlockUpdates);
    m_circuits.tick();
    m_phases.lap(Phase::Signal);
    m_blockSimulation.processLiquids();
    m_phases.lap(Phase::Liquids);
    m_fire.tick();
    m_phases.lap(Phase::Fire);
    m_health.tick();
    m_interaction.swingWhileMining();
    m_stands.tick();
    m_creatures.tick();
    m_environment.applyContactDamage();
    m_burning.tick();
    m_environment.applySuffocation();
    m_phases.lap(Phase::Mobs);
    m_plants.plantItems(m_items.tick());
    m_phases.lap(Phase::Items);
    m_plants.tick();
    m_animals.tick();
    m_phases.lap(Phase::Plants);
    m_fallingBlocks.tick();
    m_phases.lap(Phase::FallingBlocks);
    m_minecarts.tick();
    m_phases.lap(Phase::Minecarts);
    m_machines.tick();
    m_screens.update();
    m_phases.lap(Phase::Machines);
}

void QcServer::reportDamagedChunks()
{
    const QList<QcChunkPos> damaged = m_state.world().takeNewlyQuarantined();
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

QcServer::ClientSync QcServer::clientSyncDue()
{
    // During a sprint the clients hear of the world at most once per interval of real time, not
    // with every tick (decision E-249); the time of day goes along then, at its sprint speed.
    const bool sprinting = m_ticks.sprinting();
    const bool world = !sprinting || m_state.clock().playerTimeNs() - m_lastSprintSyncNs >= QcTickScheduler::intervalNs;
    if (sprinting && world) {
        m_lastSprintSyncNs = m_state.clock().playerTimeNs();
    }

    // Otherwise the time goes out once per second of the players' time.
    constexpr qint64 secondNs = 1'000'000'000;
    const bool second = m_state.clock().playerTimeNs() / secondNs != (m_state.clock().playerTimeNs() - m_state.clock().lastTickNs()) / secondNs;
    return {world, sprinting ? world : second};
}

void QcServer::refillInputBudget(Client &client) const
{
    // One input per 50 ms of the real time the tick stands for, whatever the tick rate; the cap
    // holds a whole tick's share, so slow rates keep the player's pace (decision E-249).
    const double share = double(m_state.clock().lastTickNs()) / double(QcTickScheduler::intervalNs);
    const double cap = std::max(double(Client::maxInputBudget), share + Client::inputMargin);
    client.input.budget = std::min(client.input.budget + share, cap);
}

void QcServer::updateClient(Client &client, ClientSync sync)
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

void QcServer::setWorldTime(double seconds)
{
    m_state.info().worldTime = std::max(0.0, seconds);
    sendTimeToAll();
}

void QcServer::sendTimeToAll()
{
    for (const auto &client : m_state.clients()) {
        if (client->state == Client::State::Playing) {
            sendTime(*client);
        }
    }
}

double QcServer::timeSpeed() const
{
    if (m_tickControl.frozen()) {
        return 0.0;
    }

    // A sprint runs the day as fast as its ticks run (decision E-249).
    const double rate = m_ticks.sprinting() ? std::max(m_ticks.sprintRate(), m_ticks.rate()) : m_ticks.rate();
    return std::min(rate / QcTickScheduler::defaultRate, QcMsg::WorldTime::maxSpeed);
}

void QcServer::sendTime(Client &client)
{
    client.connection->send(QcMsg::WorldTime{m_state.info().worldTime, QcDayCycle::dayLength, m_state.info().daylightCycle, timeSpeed()});
}

void QcServer::handleSetAppearance(Client &client, const QcAppearance &appearance)
{
    client.appearance = QcProfileValidation::appearance(m_state.content(), appearance);

    // Others that know the player get the spawn again with the new look.
    m_state.entities().markStale(client.id);
}
