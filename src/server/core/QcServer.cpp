#include "server/core/QcServer.h"

#include "common/auth/QcProfileValidation.h"
#include "common/physics/QcPhysics.h"
#include "common/protocol/QcChatText.h"
#include "common/protocol/QcConnection.h"
#include "common/protocol/QcProtocol.h"
#include "common/protocol/QcTransport.h"
#include "common/world/QcDayCycle.h"
#include "server/blockentities/QcServerSigns.h"
#include "server/core/QcServerLogging.h"
#include "server/core/QcServerSetup.h"
#include "server/gameplay/QcLineOfSight.h"
#include "server/gameplay/QcSpectatorRules.h"
#include "server/hanging/QcServerHanging.h"
#include "server/maps/QcServerMaps.h"
#include "server/mobs/QcNameTags.h"

#include <QTimer>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// Keeps the local player's wish to pause (decision E-877) and returns true. A network client's wish
// never pauses the server: it is dropped (false), and the log says so once per connection.
bool notePauseRequest(QcServerClient &client, bool paused)
{
    if (client.local) {
        client.pauseRequested = paused;
        return true;
    }
    if (paused && !client.pauseRefusalLogged) {
        client.pauseRefusalLogged = true;
        qCInfo(lcServer).noquote() << u"Ignoring the pause request of network player %1: only the host's own player pauses the world"_s.arg(client.name);
    }
    return false;
}

} // namespace

QcServer::QcServer(QcServerConfig config, std::shared_ptr<const QcContentRegistry> content,
                   std::unique_ptr<QcWorldGenerator> generator, QObject *parent)
    : QObject(parent)
    , m_state(std::move(config), std::move(content), std::move(generator))
    , m_pause(m_state.config().pauseWhenEmptySeconds)
    , m_sounds(m_state, m_ticks)
    , m_blockUpdates(m_state)
    , m_blocks(m_state, m_sounds, m_blockUpdates)
    , m_fallingBlocks(m_state, m_sounds, m_blockUpdates, m_blocks, m_health)
    , m_fire(m_state, m_blocks, m_sounds)
    , m_weather(m_state)
    , m_blockSimulation(m_state, m_sounds, m_blockUpdates, m_blocks, m_fallingBlocks)
    , m_circuits(m_state, m_sounds, m_blocks)
    , m_screens(m_state, m_blocks)
    , m_machines(m_state, m_sounds)
    , m_inventory(m_state, m_screens, m_sounds)
    , m_crafting(m_state, m_inventory)
    , m_ignition(m_state, m_fire, m_sounds, m_inventory)
    , m_health(m_state, m_sounds, m_inventory)
    , m_kitchen(m_state, m_sounds, m_blocks, m_inventory, m_health)
    , m_workstations(m_state, m_sounds, m_inventory, m_screens, m_blocks)
    , m_explosions(m_state, m_blocks, m_sounds, m_fire, m_health)
    , m_minecarts(m_state, m_sounds, m_inventory, m_screens)
    , m_gameModes(m_state, m_inventory, m_crafting, m_minecarts)
    , m_stands(m_state, m_sounds, m_inventory)
    , m_animals(m_state, m_sounds, m_inventory)
    , m_plants(m_state, m_sounds, m_blocks, m_inventory)
    , m_golems(m_state, m_sounds, m_blocks, m_inventory)
    , m_interaction(m_state, {m_sounds, m_blocks, m_inventory, m_crafting, m_screens, m_stands, m_minecarts, m_animals, m_plants, m_health, m_circuits,
                              m_ignition, m_golems, m_kitchen, m_workstations})
    , m_items(m_state, m_sounds, m_inventory)
    , m_experience(m_state, m_sounds)
    , m_combat(m_state, m_sounds, m_inventory)
    , m_creatures(m_state, m_sounds, m_health, m_blocks)
    , m_projectiles(m_state, m_sounds, m_health, m_inventory)
    , m_trading(m_state, m_sounds, m_inventory, m_screens)
    , m_environment(m_state, m_sounds, m_health)
    , m_burning(m_state, m_sounds, m_health)
    , m_effects(m_state, m_sounds, m_health)
    , m_lightning(m_state, m_sounds, m_health, m_ignition)
    , m_snowfall(m_state, m_blocks, m_weather)
    , m_entitySync(m_state)
    , m_chunkLoading(m_state, m_blocks, m_blockUpdates)
    , m_chunkStreaming(m_state, m_chunkLoading)
    , m_persistence(m_state, m_blocks, m_blockUpdates, m_chunkLoading)
    , m_commands(m_state, {m_blocks, m_blockUpdates,
                           {m_inventory, m_health, m_stands, m_gameModes, m_minecarts, [this](double seconds) { setWorldTime(seconds); }},
                           {m_phases, m_ticks, [this] { return save(); }, [this] { emit stopRequested(); }},
                           {m_ticks, m_tickControl, m_phases, [this] { sendTimeToAll(); }},
                           m_weather})
    , m_join(m_state, {m_persistence, m_inventory, m_health, m_gameModes, m_chunkStreaming, [this](Client &client) {
                           sendTime(client);
                           m_weather.sendTo(client);
                       }})
    , m_login(m_state, [this](Client &client) { m_join.join(client); })
    , m_admin(m_state, m_commands, m_phases, m_ticks)
    , m_tickTimer(new QTimer(this))
    , m_debug(m_state, m_blockUpdates, m_chunkLoading, m_phases, m_ticks)
    , m_systems{m_fire, m_ignition, m_burning, m_health, m_weather, m_lightning, m_snowfall, m_minecarts, m_experience, m_projectiles, m_circuits,
                m_explosions}
{
    static_assert(QcTickScheduler::intervalNs == static_cast<qint64>(QcPhysics::tickDuration * 1e9));
    m_tickTimer->setTimerType(Qt::PreciseTimer);
    m_tickTimer->setSingleShot(true);
    connect(m_tickTimer, &QTimer::timeout, this, &QcServer::runDueTicks);
    m_circuits.setIgnite([this](QcBlockPos pos) { return m_ignition.strike(pos); });
    m_blockSimulation.setIgnition(&m_ignition);
    // Explosives catch from flint and steel, fire and power (decision E-601); unlit campfires only from
    // flint and steel and dispensers (decision E-772).
    const auto light = [this](QcBlockPos pos) { return m_explosions.light(pos); };
    m_ignition.setLight([this](QcBlockPos pos) { return m_kitchen.light(pos) || m_explosions.light(pos); });
    m_fire.setLight(light);
    m_circuits.setLight(light);
    m_projectiles.setBlockHit([this](const QcProjectileManager::BlockHit &hit) {
        m_circuits.hitTarget(hit.block, QcServerCircuits::targetStrength(hit.block, hit.face, hit.point), hit.stuck);
    });
    m_circuits.setEquipPlayer([this](QcServerClient &client, const QcEquipment &equipment) { m_inventory.equipmentWorn(client, equipment, false); });

    // What the weather does (decision E-396): rain puts out fires and burning bodies and wets soil,
    // lightning hurts and lights fires.
    const auto rainsOn = [this](QcBlockPos pos) { return m_weather.isRainingAt(pos); };
    m_fire.setRainCheck(rainsOn);
    m_burning.setRainCheck(rainsOn);
    m_state.growth().setRainCheck(rainsOn);
    // Villagers ask about a column: rain on its top, below the snow line of high air.
    m_state.entities().creatures().villagers().setRainCheck([this](QcBlockPos pos) {
        return m_weather.isRainingAt({pos.x, m_weather.columnTop(pos.x, pos.z) + 1, pos.z});
    });
    // Rain on the cell above the bobber's water, so not under a roof (decision E-503).
    m_projectiles.fishing().setRainCheck([this](QcBlockPos pos) { return m_weather.isRainingAt(pos.offset(0, 1, 0)); });
    m_weather.setStrikeHook([this](QcBlockPos pos) { m_lightning.strike(pos); });

    addUpkeepSystems();
    addSimulationSystems();
    addClientSystems();
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

    const QStringList accessWarnings = m_state.access().open(m_state.config().worldDirectory, m_state.config().operators);
    m_persistence.noteWarnings(accessWarnings + m_state.identities().open(m_state.config().worldDirectory));

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
    if (const std::optional<QString> lag = m_lag.flush()) {
        qCInfo(lcLag).noquote() << *lag;
    }

    // Open crafting grids and stonecutters are emptied like on a disconnect, so overflow lands in the
    // saved world.
    for (const auto &client : m_state.clients()) {
        // Also clients that already disconnected: removeClosedClients() will not run any more.
        if (client->joined) {
            m_crafting.returnCraftingGrid(*client);
            m_inventory.returnScreenInputs(*client);
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
    // Someone connecting resumes a paused server at once, so the login runs (decision E-877).
    updatePause();
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

    // The blocks a client predicted for this action (decision E-853) are acknowledged with the next
    // world sync (QcServer::updateClient), also when the action is refused, dropped or does nothing.
    client.pendingBlockAck = std::max(client.pendingBlockAck, QcProtocol::predictionSequence(message));

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

    // A spectator only looks (decision E-730): what would touch the world or the inventory is
    // dropped, and the client gets its true inventory back.
    if (!QcSpectatorRules::allows(client, message)) {
        client.mining.pos.reset();
        m_inventory.send(client);
        return;
    }

    if (!handlePlayerMessage(client, message) && !handleInventoryMessage(client, message)) {
        qCWarning(lcServer) << "Ignoring unexpected message type" << message.index() << "from" << client.name;
    }
}

bool QcServer::withinRateLimit(Client &client, const QcMessage &message)
{
    const std::optional<QcRateLimiter::Kind> kind = QcRateLimiter::kindOf(message);
    // The local player (in-process host, the operator) is not limited in chat and in the admin
    // channel: command bursts of scripts and benchmark scenarios must arrive complete. All other
    // kinds stay limited for him.
    const bool exempt = kind && client.local && (*kind == QcRateLimiter::Kind::Chat || *kind == QcRateLimiter::Kind::Admin);
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
        if (!m_stands.hit(client, attack->id) && !m_minecarts.hit(client, attack->id)
            && !QcServerHanging(m_state, m_sounds, m_inventory).hit(client, attack->id)) {
            m_combat.attack(client, attack->id);
        }
    } else if (const auto *useEntity = std::get_if<QcMsg::UseEntity>(&message)) {
        if (!QcNameTags::use(m_state, client, useEntity->id) && !m_stands.use(client, useEntity->id) && !m_minecarts.use(client, useEntity->id)
            && !QcServerHanging(m_state, m_sounds, m_inventory).use(client, useEntity->id) && !m_trading.open(client, useEntity->id)) {
            m_interaction.useEntity(client, useEntity->id);
        }
    } else if (std::holds_alternative<QcMsg::Respawn>(message)) {
        m_health.respawn(client);
    } else if (const auto *chat = std::get_if<QcMsg::ChatInput>(&message)) {
        m_commands.chat(client, chat->text.left(256));
    } else if (std::holds_alternative<QcMsg::Disconnect>(message)) {
        client.connection->close();
    } else if (const auto *sign = std::get_if<QcMsg::EditSign>(&message)) {
        QcServerSigns(m_state).edit(client, *sign);
    } else if (const auto *debug = std::get_if<QcMsg::DebugSubscribe>(&message)) {
        m_debug.subscribe(client, debug->flags);
    } else if (const auto *pause = std::get_if<QcMsg::PauseRequest>(&message)) {
        if (notePauseRequest(client, pause->paused)) {
            updatePause();
        }
    } else if (!m_admin.handle(client, message)) {
        return false;
    }

    return true;
}

void QcServer::consumeHeld(Client &client)
{
    // A backpack used in the air opens in the hand (decision E-833).
    if (const std::optional<QList<QcItemStack>> leftOver = m_screens.openHeld(client)) {
        for (const QcItemStack &stack : *leftOver) {
            m_inventory.giveOrThrow(client, stack);
        }
        if (!leftOver->isEmpty()) {
            m_inventory.send(client);
        }
        return;
    }
    if (!QcServerMaps(m_state, m_inventory).useHeld(client)) {
        m_health.consume(client);
    }
}

bool QcServer::handleInventoryMessage(Client &client, const QcMessage &message)
{
    if (m_workstations.handleMessage(client, message)) {
        return true;
    }
    if (const auto *select = std::get_if<QcMsg::SelectHotbarSlot>(&message)) {
        client.inventory.setSelectedHotbarSlot(select->slot);
        client.mining.pos.reset();
        m_inventory.send(client);
    } else if (const auto *move = std::get_if<QcMsg::MoveInventoryItem>(&message)) {
        client.mining.pos.reset();
        if (!m_workstations.stonecutter.takeOutput(client, *move)) {
            m_inventory.moveItem(client, *move);
        }
    } else if (const auto *quick = std::get_if<QcMsg::QuickMoveItem>(&message)) {
        client.mining.pos.reset();
        if (quick->slot == QcMsg::QuickMoveItem::craftingResultSlot) {
            m_crafting.craftAll(client);
        } else if (!m_workstations.stonecutter.takeAllOutput(client, quick->slot)) {
            m_inventory.quickMove(client, *quick);
        }
    } else if (std::holds_alternative<QcMsg::TakeCraftingResult>(message)) {
        m_crafting.takeCraftingResult(client);
    } else if (const auto *craft = std::get_if<QcMsg::CraftRecipe>(&message)) {
        m_crafting.craftRecipe(client, *craft);
    } else if (std::holds_alternative<QcMsg::CloseCrafting>(message)) {
        m_crafting.closeCrafting(client);
    } else if (std::holds_alternative<QcMsg::ConsumeItem>(message)) {
        consumeHeld(client);
    } else if (std::holds_alternative<QcMsg::CloseMachine>(message)) {
        m_screens.close(client, false);
    } else if (const auto *trade = std::get_if<QcMsg::Trade>(&message)) {
        m_trading.trade(client, *trade);
    } else if (std::holds_alternative<QcMsg::CloseTrade>(message)) {
        m_trading.close(client, false);
    } else if (const auto *drop = std::get_if<QcMsg::DropItem>(&message)) {
        m_inventory.dropItem(client, *drop);
    } else if (const auto *look = std::get_if<QcMsg::SetAppearance>(&message)) {
        handleSetAppearance(client, look->appearance);
    } else if (const auto *creative = std::get_if<QcMsg::CreativeItem>(&message)) {
        m_gameModes.creativeItem(client, *creative);
    } else if (const auto *pick = std::get_if<QcMsg::PickBlock>(&message)) {
        m_gameModes.pickBlock(client, pick->pos);
    } else if (std::holds_alternative<QcMsg::ThrowItem>(message)) {
        m_projectiles.useHeld(client);
    } else if (const auto *draw = std::get_if<QcMsg::DrawLauncher>(&message)) {
        m_projectiles.launchers().draw(client, draw->action);
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
            list.players.append({client->id, client->name, m_state.permissionOf(*client) == QcPermission::Operator, client->gameMode == QcGameMode::Spectator});
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
    // A command while the empty server rests may change the world: it is saved with the next
    // paused tick (decision E-877).
    m_pause.noteChange();
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
    // The speed follows the player's effects as the server knows them, never the client's word.
    input.speedFactor = static_cast<float>(client.effects.speedFactor(m_state.content()));

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
    client.motion.stepPlayer(input, m_state.world(), QcPhysics::tickDuration, client.rules().movement());
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
    if (m_tickControl.frozen() || m_pause.paused()) {
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
