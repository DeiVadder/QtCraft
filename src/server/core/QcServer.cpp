#include "QcServer.h"

#include "QcChatText.h"
#include "QcConnection.h"
#include "QcDayCycle.h"
#include "QcPhysics.h"
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
    , m_sounds(m_state)
    , m_blockUpdates(m_state)
    , m_blocks(m_state, m_sounds, m_blockUpdates)
    , m_blockSimulation(m_state, m_sounds, m_blockUpdates, m_blocks)
    , m_machineScreens(m_state, m_sounds, m_blocks)
    , m_inventory(m_state, m_machineScreens)
    , m_interaction(m_state, {m_sounds, m_blocks, m_inventory, m_machineScreens})
    , m_itemsAndPlants(m_state, m_sounds, m_blocks, m_inventory)
    , m_combat(m_state, m_sounds, m_inventory)
    , m_entitySync(m_state)
    , m_chunkStreaming(m_state)
    , m_chat(m_state, {m_blocks, m_blockUpdates, m_inventory, m_combat},
             {[this] { return save(); }, [this](double seconds) { setWorldTime(seconds); }})
    , m_persistence(m_state, m_blocks, m_blockUpdates)
    , m_tickTimer(new QTimer(this))
{
    static_assert(QcTickScheduler::intervalNs == qint64(QcPhysics::tickDuration * 1e9));
    m_tickTimer->setTimerType(Qt::PreciseTimer);
    m_tickTimer->setSingleShot(true);
    connect(m_tickTimer, &QTimer::timeout, this, &QcServer::runDueTicks);
}

QcServer::~QcServer() = default;

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

    // A new world gets what its generator sets up once (decision E-221).
    if (!m_state.info.setupDone) {
        QcServerSetup::apply(m_state, m_blockUpdates, m_state.world.generator().setup());
    }
    return {};
}

QcStatus QcServer::save()
{
    return m_persistence.save();
}

QcStatus QcServer::stop()
{
    m_tickTimer->stop();

    // Open crafting grids are emptied like on a disconnect, so overflow lands in the saved world.
    for (const auto &client : m_state.clients) {
        // Also clients that already disconnected: removeClosedClients() will not run any more.
        if (client->joined) {
            m_inventory.returnCraftingGrid(*client);
        }
    }

    const QcStatus saved = save();
    if (!saved) {
        qCCritical(lcServer) << "Saving failed:" << saved.error();
    }

    for (const auto &client : m_state.clients) {
        client->connection->send(QcMsg::Disconnect{u"Server stopped"_s});
        client->connection->close();
    }
    return saved;
}

void QcServer::addClient(std::unique_ptr<QcTransport> transport, ClientOrigin origin)
{
    auto client = std::make_unique<Client>();
    client->local = origin == ClientOrigin::Local;
    Client *raw = client.get();
    client->connection = std::make_unique<QcConnection>(std::move(transport), QcProtocol::Peer::Client,
                                                        [this, raw](QcMessage &&message) { handleMessage(*raw, std::move(message)); });
    connect(client->connection.get(), &QcConnection::closed, this, [raw] { raw->state = Client::State::Closed; });

    m_state.clients.push_back(std::move(client));
    emit clientCountChanged(clientCount());
}

void QcServer::handleMessage(Client &client, QcMessage &&message)
{
    if (client.state == Client::State::Closed) {
        return;
    }

    if (const auto *hello = std::get_if<QcMsg::Hello>(&message)) {
        handleHello(client, *hello);
        return;
    }

    if (client.state != Client::State::Playing) {
        client.connection->send(QcMsg::Disconnect{u"Expected Hello"_s});
        client.connection->close();
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
        client.miningPos.reset();
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
    if (!kind || exempt || client.rateLimiter.allow(*kind, m_state.tickCount)) {
        return true;
    }

    if (*kind == QcRateLimiter::Kind::Mining) {
        resendDroppedMiningTarget(client, message);
    }

    // Dropped without a kick; the log names the spammer at most once a second per kind.
    if (const quint64 dropped = client.rateLimiter.takeDropReport(*kind, m_state.tickCount); dropped > 0) {
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
    if (!pos || !m_state.isWithinReach(client, *pos)) {
        return;
    }

    // A flood must not turn into a flood of answers: each position once per tick, and only as many
    // positions as the real client mines in one tick.
    if (client.miningResendTick != m_state.tickCount) {
        client.miningResendTick = m_state.tickCount;
        client.miningResends.clear();
    }
    if (client.miningResends.contains(*pos) || client.miningResends.size() >= Client::maxMiningResendsPerTick) {
        return;
    }

    if (const std::optional<QcBlockId> block = m_state.world.blockAt(*pos)) {
        client.miningResends.append(*pos);
        client.connection->send(QcMsg::BlockUpdate{*pos, *block});
    }
}

bool QcServer::handlePlayerMessage(Client &client, const QcMessage &message)
{
    if (const auto *input = std::get_if<QcMsg::PlayerInput>(&message)) {
        handleInput(client, *input);
    } else if (const auto *start = std::get_if<QcMsg::StartMining>(&message)) {
        client.miningPos = start->pos;
        client.miningStartTick = m_state.tickCount;
    } else if (std::holds_alternative<QcMsg::CancelMining>(message)) {
        client.miningPos.reset();
    } else if (const auto *finish = std::get_if<QcMsg::FinishMining>(&message)) {
        m_interaction.finishMining(client, finish->pos);
    } else if (const auto *use = std::get_if<QcMsg::UseItem>(&message)) {
        m_interaction.useItem(client, *use);
    } else if (const auto *attack = std::get_if<QcMsg::AttackEntity>(&message)) {
        m_combat.attack(client, attack->id);
    } else if (const auto *useEntity = std::get_if<QcMsg::UseEntity>(&message)) {
        m_interaction.useEntity(client, useEntity->id);
    } else if (std::holds_alternative<QcMsg::Respawn>(message)) {
        m_combat.respawn(client);
    } else if (const auto *chat = std::get_if<QcMsg::ChatInput>(&message)) {
        m_chat.handle(client, chat->text.left(256));
    } else if (std::holds_alternative<QcMsg::Disconnect>(message)) {
        client.connection->close();
    } else {
        return false;
    }

    return true;
}

bool QcServer::handleInventoryMessage(Client &client, const QcMessage &message)
{
    if (const auto *select = std::get_if<QcMsg::SelectHotbarSlot>(&message)) {
        client.inventory.setSelectedHotbarSlot(select->slot);
        client.miningPos.reset();
        m_inventory.send(client);
    } else if (const auto *move = std::get_if<QcMsg::MoveInventoryItem>(&message)) {
        client.miningPos.reset();
        m_inventory.moveItem(client, *move);
    } else if (const auto *quick = std::get_if<QcMsg::QuickMoveItem>(&message)) {
        client.miningPos.reset();
        m_inventory.quickMove(client, *quick);
    } else if (std::holds_alternative<QcMsg::TakeCraftingResult>(message)) {
        m_inventory.takeCraftingResult(client);
    } else if (const auto *craft = std::get_if<QcMsg::CraftRecipe>(&message)) {
        m_inventory.craftRecipe(client, *craft);
    } else if (std::holds_alternative<QcMsg::CloseCrafting>(message)) {
        m_inventory.closeCrafting(client);
    } else if (std::holds_alternative<QcMsg::ConsumeItem>(message)) {
        m_combat.consume(client);
    } else if (std::holds_alternative<QcMsg::CloseMachine>(message)) {
        m_machineScreens.close(client, false);
    } else if (const auto *drop = std::get_if<QcMsg::DropItem>(&message)) {
        m_inventory.dropItem(client, *drop);
    } else if (const auto *look = std::get_if<QcMsg::SetAppearance>(&message)) {
        handleSetAppearance(client, look->appearance);
    } else {
        return false;
    }

    return true;
}

void QcServer::handleHello(Client &client, const QcMsg::Hello &hello)
{
    if (client.state != Client::State::AwaitingHello) {
        return;
    }

    if (hello.protocolVersion != QcProtocol::version) {
        client.connection->send(QcMsg::Disconnect{
            u"Protocol version mismatch: server %1, client %2"_s.arg(QcProtocol::version).arg(hello.protocolVersion)});
        client.connection->close();
        return;
    }

    client.entityId = m_state.newEntityId();
    client.name = hello.playerName.left(32);
    client.appearance = validAppearance(hello.appearance);
    if (restorePlayer(client)) {
        return;
    }

    client.state = Client::State::Playing;
    client.joined = true;
    client.motion.position = m_state.spawnPosition();
    client.fallPeak = client.motion.position.y;
    client.stateDirty = true;

    qCInfo(lcServer) << "Player" << client.name << "joined as entity" << client.entityId;
    sendWelcome(client, false);
    m_chunkStreaming.stream(client);
}

bool QcServer::restorePlayer(Client &client)
{
    if (!m_persistence.hasStorage()) {
        return false;
    }

    QStringList warnings;
    const QcResult<std::optional<QcPlayerData>> stored = m_persistence.readPlayer(client.name, warnings);
    for (const QString &warning : std::as_const(warnings)) {
        qCWarning(lcServer) << warning;
    }

    if (!stored) {
        client.connection->send(QcMsg::Disconnect{u"Cannot load player data: %1"_s.arg(stored.error())});
        client.connection->close();
        return true;
    }

    if (!*stored) {
        return false;
    }

    const QcPlayerData &player = **stored;
    client.motion = player.motion;
    client.fallPeak = client.motion.position.y;
    client.yaw = player.yaw;
    client.pitch = player.pitch;
    client.inventory = player.inventory;
    client.health = std::clamp(player.health, 0.0, playerMaxHealth);
    client.hunger.restore(player.food, player.exhaustion);

    // Who left while dead comes back to the death screen and respawns from there.
    client.dead = client.health <= 0.0;
    client.state = Client::State::Playing;
    client.joined = true;
    client.stateDirty = true;

    qCInfo(lcServer) << "Player" << client.name << "rejoined as entity" << client.entityId;
    sendWelcome(client, true);
    m_state.world.loadChunk(QcChunkStreaming::chunkOf(client.motion.position));
    m_chunkStreaming.stream(client);

    return true;
}

void QcServer::sendWelcome(Client &client, bool rejoined)
{
    client.connection->send(QcMsg::Welcome{QcProtocol::version, client.entityId, m_state.config.worldName, m_state.config.seed,
                                           rejoined ? client.yaw : 0.0f, rejoined ? client.pitch : 0.0f, rejoined});
    client.connection->send(QcMsg::ContentSync{m_state.content});
    m_inventory.send(client);
    m_combat.sendHealth(client);
    m_combat.sendFood(client);
    sendTime(client);
    sendOpenWarnings(client);
}

void QcServer::sendOpenWarnings(Client &client)
{
    // The player should know why blocks or items of a removed mod are gone.
    constexpr qsizetype shown = 5;
    const QStringList first = m_persistence.openWarnings().first(std::min(shown, m_persistence.openWarnings().size()));

    for (const QString &warning : first) {
        client.connection->send(QcChatText::plain(warning));
    }

    if (m_persistence.openWarnings().size() > shown) {
        client.connection->send(QcChatText::message(QT_TRANSLATE_NOOP("QcServerMessages", "... and %1 more warnings in the log"),
            {QString::number(m_persistence.openWarnings().size() - shown)}));
    }
}

void QcServer::handleInput(Client &client, const QcMsg::PlayerInput &message)
{
    if (client.inputBudget <= 0) {
        return;
    }

    if (client.dead) {
        // Acknowledge without moving so the client's queue of predicted inputs drains.
        client.lastInputSequence = message.input.sequence;
        client.stateDirty = true;
        return;
    }

    --client.inputBudget;

    // The decoder already rejects non-finite values; ranges are enforced here.
    QcPlayerInput input = message.input;
    input.forward = std::clamp(input.forward, -1.0f, 1.0f);
    input.strafe = std::clamp(input.strafe, -1.0f, 1.0f);
    input.pitch = std::clamp(input.pitch, -90.0f, 90.0f);
    input.yaw = std::fmod(input.yaw, 360.0f);

    const bool wasOnGround = client.motion.onGround;
    const bool wasInWater = client.motion.inWater;
    const QcVec3 previous = client.motion.position;
    client.previousPosition = previous;
    QcPhysics::stepPlayer(client.motion, input, m_state.world, QcPhysics::tickDuration);
    m_combat.updateHealth(client, wasOnGround, wasInWater, previous.y);

    client.yaw = input.yaw;
    client.pitch = input.pitch;
    client.sneaking = input.sneak;
    client.sprinting = input.sprint && input.forward > 0.0f && !input.sneak;

    const bool jumped = input.jump && wasOnGround && !client.motion.onGround && client.motion.velocity.y > 0.0;
    const double distance = std::hypot(client.motion.position.x - previous.x, client.motion.position.z - previous.z);
    client.hunger.exhaust(QcHunger::movementExhaustion(distance, client.sprinting, client.motion.inWater, jumped));

    client.lastInputSequence = input.sequence;
    client.stateDirty = true;
}

void QcServer::runDueTicks()
{
    waitUntilDue();
    armTickTimer(m_ticks.runDue([this] { tick(); }));
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
    ++m_state.tickCount;
    if (m_state.info.daylightCycle) {
        m_state.info.worldTime += QcPhysics::tickDuration;
    }
    m_state.mobs.setNaturalSpawning(m_state.info.mobSpawning);

    removeClosedClients();
    autosave();
    m_state.world.collectGeneratedChunks();
    m_blockUpdates.handleLoadedChunks();

    m_blockSimulation.processBlockUpdates();
    m_blockSimulation.processLiquids();
    m_combat.tick();
    m_combat.applyContactDamage();
    m_itemsAndPlants.tick();
    m_blockSimulation.tickFallingBlocks();
    m_machineScreens.tick();

    for (const auto &client : m_state.clients) {
        if (client->state == Client::State::Playing) {
            updateClient(*client);
        }
    }

    for (const auto &client : m_state.clients) {
        client->swung = false;
        client->hurtThisTick = false;
    }

    m_state.died.clear();

    // Drop chunks no player needs any more.
    m_chunkStreaming.unloadUnneeded();
}

void QcServer::autosave()
{
    if (!m_persistence.hasStorage() || m_state.config.autosaveSeconds <= 0
        || m_state.tickCount % quint64(m_state.config.autosaveSeconds * QcPhysics::tickRate) != 0) {
        return;
    }

    if (const QcStatus saved = save(); !saved) {
        qCWarning(lcServer) << "Autosave failed:" << saved.error();
        for (const auto &client : m_state.clients) {
            client->connection->send(QcChatText::message(QT_TRANSLATE_NOOP("QcServerMessages", "Autosave failed: %1"),
                {saved.error()}));
        }
    }
}

void QcServer::updateClient(Client &client)
{
    client.inputBudget = std::min(client.inputBudget + 1, QcServerClient::maxInputBudget);
    m_chunkStreaming.stream(client);
    m_entitySync.sync(client);

    if (m_state.tickCount % quint64(QcPhysics::tickRate) == 0) {
        sendTime(client);
    }

    if (client.hunger.food() != client.sentFood) {
        m_combat.sendFood(client);
    }

    if (client.stateDirty) {
        client.connection->send(QcMsg::PlayerState{client.lastInputSequence, client.motion});
        client.stateDirty = false;
    }
}

void QcServer::removeClosedClients()
{
    for (const auto &client : m_state.clients) {
        if (client->state == Client::State::Closed && client->joined) {
            m_inventory.returnCraftingGrid(*client);
            // Failures are logged by savePlayer; the client is gone and cannot be told.
            [[maybe_unused]] const QcStatus saved = m_persistence.savePlayer(*client);
        }
    }

    const auto removed = std::erase_if(m_state.clients, [](const std::unique_ptr<Client> &client) {
        return client->state == Client::State::Closed;
    });

    if (removed > 0) {
        emit clientCountChanged(clientCount());
    }
}

void QcServer::setWorldTime(double seconds)
{
    m_state.info.worldTime = std::max(0.0, seconds);

    for (const auto &client : m_state.clients) {
        if (client->state == Client::State::Playing) {
            sendTime(*client);
        }
    }
}

void QcServer::sendTime(Client &client)
{
    client.connection->send(QcMsg::WorldTime{m_state.info.worldTime, QcDayCycle::dayLength, m_state.info.daylightCycle});
}

QcAppearance QcServer::validAppearance(const QcAppearance &requested) const
{
    QcAppearance result = requested;
    const QcAppearanceDefinition *outfit = m_state.content->appearance(requested.outfit);
    if (!outfit || outfit->kind != QcAppearanceDefinition::Kind::Outfit || !outfit->playable) {
        // The default outfit, or else any outfit the content offers.
        result.outfit = QcAppearance().outfit;
        const QcAppearanceDefinition *fallback = m_state.content->appearance(result.outfit);
        if (!fallback || fallback->kind != QcAppearanceDefinition::Kind::Outfit || !fallback->playable) {
            const auto any = std::ranges::find_if(m_state.content->appearances(), [](const QcAppearanceDefinition &definition) {
                return definition.kind == QcAppearanceDefinition::Kind::Outfit && definition.playable;
            });
            result.outfit = any != m_state.content->appearances().end() ? any->name : QString();
        }
    }

    const QcAppearanceDefinition *hair = m_state.content->appearance(requested.hair);
    if (!hair || hair->kind != QcAppearanceDefinition::Kind::Hair || !hair->playable) {
        result.hair.clear();
    }

    for (quint32 *color : {&result.skinColor, &result.hairColor, &result.shirtColor, &result.pantsColor}) {
        *color &= 0xFFFFFFu;
    }
    return result;
}

void QcServer::handleSetAppearance(Client &client, const QcAppearance &appearance)
{
    client.appearance = validAppearance(appearance);

    // Others that know the player get the spawn again with the new look.
    for (const auto &other : m_state.clients) {
        if (other->knownEntities.contains(client.entityId)) {
            other->staleEntities.insert(client.entityId);
        }
    }
}
