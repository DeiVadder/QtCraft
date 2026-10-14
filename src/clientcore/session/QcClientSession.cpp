#include "QcClientSession.h"

#include "QcChatText.h"
#include "QcConnection.h"
#include "QcMining.h"
#include "QcPhysics.h"
#include "QcProtocol.h"
#include "QcSoundKeys.h"
#include "QcTransport.h"

#include <QLoggingCategory>
#include <QTimer>
#include <QtMath>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// The box the crosshair hits an entity in.
QcAabb targetBoxOf(const QcContentRegistry &content, const QcClientEntity &entity)
{
    if (entity.isArmourStand()) {
        const double scale = entity.stand.small ? 0.5 : 1.0;
        return QcPhysics::boxAt(entity.position, QcStandPose::bodyWidth * scale, QcStandPose::bodyHeight * scale);
    }

    // Young animals are drawn and aimed at half size (decision E-293).
    const QcMobDefinition *creature = entity.creature(content);
    const double scale = (entity.flags & QcMsg::Young) != 0 ? QcMsg::youngScale : 1.0;
    const double width = creature ? creature->width : QcPhysics::playerWidth;
    const double height = creature ? creature->height : QcPhysics::playerHeight;
    return QcPhysics::boxAt(entity.position, width * scale, height * scale);
}

// Whether using `item` on the entity is a use of the creature (UseEntity): food, armour, its
// catching item or its breeding food (decision E-293).
bool usesOnCreature(const QcContentRegistry &content, const QcClientEntity &entity, const QcItemDefinition &item)
{
    const QcMobDefinition *creature = entity.creature(content);
    return creature && (item.food > 0 || item.armour || creature->catchWith == item.name || creature->breeding.food.contains(item.name));
}

Q_LOGGING_CATEGORY(lcSession, "qtcraft.session")

// Unacknowledged inputs beyond this are dropped; the server would reject them anyway.
constexpr qsizetype maxPendingInputs = 100;

bool chunkUsesKnownBlocks(const QcChunk &chunk, const QcContentRegistry &content)
{
    for (int i = 0; i < QcWorld::sectionCount; ++i) {
        for (const QcBlockId id : chunk.section(i).palette()) {
            if (!content.isValidBlock(id)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

QcClientSession::QcClientSession(std::unique_ptr<QcTransport> transport, QString playerName, QObject *parent)
    : QObject(parent)
    , m_playerName(std::move(playerName))
    , m_tickTimer(new QTimer(this))
{
    m_connection = std::make_unique<QcConnection>(std::move(transport), QcProtocol::Peer::Server,
                                                  [this](QcMessage &&message) { handleMessage(std::move(message)); });
    connect(m_connection.get(), &QcConnection::closed, this, &QcClientSession::onClosed);
    m_gameMode.setSender([this](const QcMessage &message) { m_connection->send(message); });

    m_tickTimer->setTimerType(Qt::PreciseTimer);
    m_tickTimer->setInterval(int(QcPhysics::tickDuration * 1000.0));
    connect(m_tickTimer, &QTimer::timeout, this, &QcClientSession::tick);
}

QcClientSession::~QcClientSession() = default;

void QcClientSession::start(TickMode mode)
{
    m_connection->send(QcMsg::Hello{QcProtocol::version, m_playerName, m_appearance});
    m_helloSent = true;
    if (mode == TickMode::Timer) {
        m_tickTimer->start();
    }
}

void QcClientSession::setIntent(const QcPlayerInput &intent)
{
    const auto finiteOr = [](float value, float fallback) { return std::isfinite(value) ? value : fallback; };
    m_intent = intent;
    m_intent.forward = std::clamp(finiteOr(intent.forward, 0.0f), -1.0f, 1.0f);
    m_intent.strafe = std::clamp(finiteOr(intent.strafe, 0.0f), -1.0f, 1.0f);
    m_intent.pitch = std::clamp(finiteOr(intent.pitch, 0.0f), -90.0f, 90.0f);
    const float yaw = std::fmod(finiteOr(intent.yaw, 0.0f), 360.0f);
    m_intent.yaw = yaw < 0.0f ? yaw + 360.0f : yaw;

    refreshTarget();
}

void QcClientSession::disconnectFromServer()
{
    if (m_state == State::Disconnected) {
        return;
    }

    m_connection->send(QcMsg::Disconnect{u"Client left"_s, {}, {}});
    m_connection->close();
}

void QcClientSession::setState(State state)
{
    if (m_state == state) {
        return;
    }

    m_state = state;
    emit stateChanged(state);
}

void QcClientSession::onClosed()
{
    m_tickTimer->stop();
    if (m_disconnectReason.isEmpty()) {
        m_disconnectReason = m_connection->lastError().isEmpty() ? u"Connection closed"_s : m_connection->lastError();
    }

    setState(State::Disconnected);
    emit disconnected(m_disconnectReason);
}

void QcClientSession::handleMessage(QcMessage &&message)
{
    if (m_state == State::Disconnected) {
        return;
    }

    if (!handleConnectionMessage(message) && !handleWorldMessage(message) && !handlePlayerMessage(message)
        && !handleEntityMessage(message) && !handleMachineMessage(message)) {
        qCWarning(lcSession) << "Ignoring unexpected message type" << message.index();
    }
}

void QcClientSession::closeWithError(const QString &reason)
{
    m_disconnectReason = reason;
    m_connection->close();
}

bool QcClientSession::handleConnectionMessage(const QcMessage &message)
{
    if (const auto *welcome = std::get_if<QcMsg::Welcome>(&message)) {
        onWelcome(*welcome);
    } else if (const auto *sync = std::get_if<QcMsg::ContentSync>(&message)) {
        onContentSync(*sync);
    } else if (const auto *disconnect = std::get_if<QcMsg::Disconnect>(&message)) {
        m_disconnectMessage = *disconnect;
        closeWithError(QcChatText::display(*disconnect));
    } else if (const auto *challenge = std::get_if<QcMsg::LoginChallenge>(&message)) {
        m_connection->send(m_login.answer(*challenge));
    } else if (const auto *players = std::get_if<QcMsg::PlayerList>(&message)) {
        m_players = players->players;
        emit playersChanged();
    } else {
        return false;
    }
    return true;
}

bool QcClientSession::handleWorldMessage(const QcMessage &message)
{
    if (const auto *chunkData = std::get_if<QcMsg::ChunkData>(&message)) {
        onChunkData(*chunkData);
    } else if (const auto *unload = std::get_if<QcMsg::ChunkUnload>(&message)) {
        m_labelGeneration += m_labels.remove(unload->pos) ? 1 : 0;
        if (m_world) {
            m_world->removeChunk(unload->pos);
            emit chunkUnloaded(unload->pos);
        }
    } else if (const auto *labels = std::get_if<QcMsg::ChunkLabels>(&message)) {
        m_labels.insert(labels->pos, labels->labels);
        ++m_labelGeneration;
    } else if (const auto *debug = std::get_if<QcMsg::ServerDebug>(&message)) {
        m_serverDebug.receive(*debug);
    } else if (const auto *update = std::get_if<QcMsg::BlockUpdate>(&message)) {
        const QcBlockId previous = m_world ? m_world->blockAt(update->pos).value_or(qcAirBlockId) : qcAirBlockId;
        if (m_world && m_content->isValidBlock(update->block) && m_world->setBlock(update->pos, update->block)) {
            emit blockChanged(update->pos, previous);
        }
    } else if (const auto *sound = std::get_if<QcMsg::SoundEvent>(&message)) {
        cue({sound->key, sound->position, sound->volume, sound->pitch, QcSoundCue::Origin::Server, m_tickCount});
    } else if (const auto *time = std::get_if<QcMsg::WorldTime>(&message)) {
        m_worldTime = *time;
    } else {
        return false;
    }

    return true;
}

bool QcClientSession::handlePlayerMessage(const QcMessage &message)
{
    if (const auto *playerState = std::get_if<QcMsg::PlayerState>(&message)) {
        reconcile(*playerState);
        if (m_state == State::Loading && m_world) {
            setState(State::Playing);
        }
    } else if (const auto *inventory = std::get_if<QcMsg::InventoryState>(&message)) {
        onInventoryState(*inventory);
    } else if (const auto *open = std::get_if<QcMsg::OpenCrafting>(&message)) {
        emit craftingOpened(open->size);
    } else if (const auto *chat = std::get_if<QcMsg::ChatMessage>(&message)) {
        emit chatReceived(QcChatText::display(*chat));
    } else if (const auto *health = std::get_if<QcMsg::PlayerHealth>(&message)) {
        m_health = *health;
        if (m_health.dead) {
            // A player comes back from death on the ground.
            m_miningRequested = false;
            m_gameMode.setFlightWanted(false);
        }
        emit healthChanged();
    } else if (const auto *food = std::get_if<QcMsg::PlayerFood>(&message)) {
        m_food = *food;
        emit foodChanged();
    } else if (const auto *mode = std::get_if<QcMsg::GameMode>(&message)) {
        m_gameMode.setMode(mode->mode);
        emit gameModeChanged();
    } else {
        return false;
    }

    return true;
}

bool QcClientSession::handleEntityMessage(const QcMessage &message)
{
    if (const auto *spawn = std::get_if<QcMsg::EntitySpawn>(&message)) {
        if (!m_entities.spawn(*spawn, m_content.get())) {
            closeWithError(u"Server sent an unknown entity type"_s);
        }
    } else if (const auto *moves = std::get_if<QcMsg::EntityMoves>(&message)) {
        m_entities.moves(*moves);
    } else if (const auto *removed = std::get_if<QcMsg::EntityRemove>(&message)) {
        m_entities.remove(*removed);
    } else {
        return false;
    }
    return true;
}

bool QcClientSession::handleMachineMessage(const QcMessage &message)
{
    const std::optional<QcClientMachineScreen::Change> change = m_machine.handle(message, m_content.get());
    if (!change) {
        return false;
    }

    switch (*change) {
    case QcClientMachineScreen::Change::Opened:
        emit machineOpened();
        break;
    case QcClientMachineScreen::Change::Changed:
        emit machineChanged();
        break;
    case QcClientMachineScreen::Change::Closed:
        emit machineClosed();
        break;
    case QcClientMachineScreen::Change::None:
        break;
    }
    return true;
}

void QcClientSession::onWelcome(const QcMsg::Welcome &welcome)
{
    if (welcome.protocolVersion != QcProtocol::version) {
        closeWithError(u"Server uses protocol version %1, this client %2"_s.arg(welcome.protocolVersion).arg(QcProtocol::version));
        return;
    }

    m_entityId = welcome.playerEntityId;
    if (welcome.restoredView) {
        QcPlayerInput intent = m_intent;
        intent.yaw = welcome.yaw;
        intent.pitch = welcome.pitch;
        setIntent(intent);
    }

    m_worldName = welcome.worldName;
    m_seed = welcome.seed;
    m_viewDistance = welcome.viewDistance;
    setState(State::Loading);

    if (m_appearancePending) {
        m_appearancePending = false;
        m_connection->send(QcMsg::SetAppearance{m_appearance});
    }
    if (debugSubscription() != 0) {
        m_connection->send(QcMsg::DebugSubscribe{debugSubscription()});
    }
}

void QcClientSession::onContentSync(const QcMsg::ContentSync &sync)
{
    if (m_content) {
        // Block ids of loaded chunks would no longer match; content changes need a reconnect.
        closeWithError(u"Server sent content twice"_s);
        return;
    }

    m_content = sync.content;
    m_world = std::make_unique<QcClientWorld>(m_content);
    emit contentReady();
}

void QcClientSession::onChunkData(const QcMsg::ChunkData &chunkData)
{
    if (!m_world || !chunkUsesKnownBlocks(*chunkData.chunk, *m_content)) {
        closeWithError(u"Server sent invalid chunk data"_s);
        return;
    }

    const QcChunkPos pos = chunkData.chunk->pos();
    m_world->setChunk(chunkData.chunk);
    emit chunkLoaded(pos);
}

void QcClientSession::onInventoryState(const QcMsg::InventoryState &inventory)
{
    if (inventory.stacks.size() != QcInventory::slotCount) {
        closeWithError(u"Server sent an invalid inventory"_s);
        return;
    }

    for (int i = 0; i < QcInventory::slotCount; ++i) {
        m_inventory.setSlot(i, inventory.stacks[i]);
    }
    m_inventory.setSelectedHotbarSlot(inventory.selected);

    if (inventory.craftingGrid.size() == 9 && (inventory.craftingSize == 2 || inventory.craftingSize == 3)) {
        m_craftingGrid = inventory.craftingGrid;
        m_craftingSize = inventory.craftingSize;
        m_craftingResult = inventory.craftingResult;
    }
    // Only items of the content are ever drawn (decision E-232).
    m_equipment = m_content ? inventory.equipment.knownTo(*m_content) : QcEquipment();

    emit inventoryChanged();
}

void QcClientSession::reconcile(const QcMsg::PlayerState &state)
{
    // A state older than one already applied would roll the prediction back; ignore it.
    if (m_hasServerState && state.lastInputSequence < m_lastAcknowledged) {
        return;
    }

    // A player saved while flying flies on after joining (decision E-284).
    if (!m_hasServerState) {
        m_gameMode.setFlightWanted(state.motion.flying);
    }
    m_lastAcknowledged = state.lastInputSequence;
    m_hasServerState = true;
    m_pendingInputs.removeIf([&state](const QcPlayerInput &input) { return input.sequence <= state.lastInputSequence; });

    QcEntityMotion replayed = state.motion;
    if (m_world) {
        for (const QcPlayerInput &input : std::as_const(m_pendingInputs)) {
            QcPhysics::stepPlayer(replayed, input, *m_world, QcPhysics::tickDuration, m_gameMode.rules().mayFly);
        }
    }
    m_lastCorrection = (replayed.position - m_motion.position).length();
    m_largestCorrection = std::max(m_largestCorrection, m_lastCorrection);
    m_motion = replayed;
}

void QcClientSession::tick()
{
    if (m_state != State::Playing || !m_world) {
        return;
    }

    ++m_tickCount;
    if (m_worldTime.running) {
        // At another tick rate of the server (or frozen) the day runs faster or slower (decision E-249).
        m_worldTime.time += QcPhysics::tickDuration * m_worldTime.speed;
    }
    m_entities.tick();

    const auto advance = [](int &ticks, int length) {
        if (ticks < 0) {
            return;
        }
        ++ticks;
        if (ticks >= length) {
            ticks = -1;
        }
    };

    advance(m_swingTicks, swingTicks);
    advance(m_useTicks, useTicks);
    advance(m_switchTicks, switchTicks);
    advance(m_landingTicks, landingTicks);

    const QcItemStack &held = m_inventory.selectedStack();
    const QcItemId heldItem = held.isEmpty() ? qcNoItem : held.item;
    if (heldItem != m_lastHeld) {
        m_lastHeld = heldItem;
        m_switchTicks = 0;
    }

    // Keep swinging while a block is being mined.
    if (m_miningPos && m_swingTicks < 0) {
        m_swingTicks = 0;
    }

    updateSounds();

    if (m_health.dead) {
        // Nothing to simulate until the server respawns the player.
        emit ticked();
        return;
    }

    QcPlayerInput input = m_intent;
    input.sequence = m_nextSequence++;
    input.flying = m_gameMode.flightWanted();
    m_previousMotion = m_motion;
    QcPhysics::stepPlayer(m_motion, input, *m_world, QcPhysics::tickDuration, m_gameMode.rules().mayFly);
    m_gameMode.stepped(m_motion.flying);
    m_lastStep = std::hypot(m_motion.position.x - m_previousMotion.position.x, m_motion.position.z - m_previousMotion.position.z);
    m_walkDistance += m_lastStep;

    if (m_motion.onGround && !m_previousMotion.onGround) {
        m_landingTicks = 0;
    }

    const QList<QcSoundCue> steps = m_sounds.local().update(m_previousMotion, m_motion, m_intent.sneak, *m_world);
    for (const QcSoundCue &step : steps) {
        cue(step);
    }

    if (m_pendingInputs.size() >= maxPendingInputs) {
        m_pendingInputs.removeFirst();
    }
    m_pendingInputs.append(input);
    m_connection->send(QcMsg::PlayerInput{input});

    updateTargetAndMining();
    emit ticked();
}

void QcClientSession::cue(QcSoundCue cue)
{
    if (m_sounds.record(cue, m_tickCount)) {
        emit soundCue(cue);
    }
}

void QcClientSession::updateSounds()
{
    m_entities.stepSounds(*m_world, [this](const QcSoundCue &step) { cue(step); });
    const QcVec3 eye = m_motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
    m_sounds.updateAmbience(*m_world, eye, m_tickCount, [this](const QcSoundCue &sound) { cue(sound); });
}

void QcClientSession::refreshTarget()
{
    if (!m_world) {
        m_target.reset();
        return;
    }

    const double yaw = qDegreesToRadians(double(m_intent.yaw));
    const double pitch = qDegreesToRadians(double(m_intent.pitch));
    const QcVec3 direction{std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
    const QcVec3 eye = m_motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};

    const QcItemStack &held = m_inventory.selectedStack();
    const bool scoops = !held.isEmpty() && m_content->isValidItem(held.item)
                        && (m_content->item(held.item).scoops || m_content->item(held.item).bottles);
    m_target = QcRaycast::cast(*m_world, eye, direction, QcRaycast::playerReach,
                               scoops ? QcRaycast::Targets::VisibleAndLiquids : QcRaycast::Targets::Visible);

    m_targetEntity.reset();
    double nearest = m_target ? m_target->distance : attackReach;
    for (const QcClientEntity &entity : m_entities.all()) {
        if (entity.isItem() || entity.isBlockEntity()) {
            continue;
        }

        const std::optional<double> distance =
            QcRaycast::intersectBox(eye, direction, targetBoxOf(*m_content, entity), std::min(nearest, attackReach));

        if (distance && *distance < nearest) {
            nearest = *distance;
            m_targetEntity = entity.id;
        }
    }
}

bool QcClientSession::attack()
{
    if (m_state != State::Playing || m_health.dead || !m_targetEntity) {
        return false;
    }

    m_connection->send(QcMsg::AttackEntity{*m_targetEntity});
    m_swingTicks = 0;
    return true;
}

void QcClientSession::setAppearance(const QcAppearance &appearance)
{
    m_appearance = appearance;
    if (m_state == State::Playing || m_state == State::Loading) {
        m_connection->send(QcMsg::SetAppearance{appearance});
    } else if (m_state == State::Connecting && m_helloSent) {
        // Hello is on its way with the old look; send the new one once welcomed.
        m_appearancePending = true;
    }
}

QcEntityPose QcClientSession::localPose(double alpha) const
{
    QcEntityPose pose;
    pose.speed = m_lastStep * QcPhysics::tickRate;
    pose.walkDistance = m_walkDistance - m_lastStep * (1.0 - alpha);
    pose.onGround = m_motion.onGround;
    pose.inWater = m_motion.inWater;
    // Sneaking in flight is descending, not crouching (decision E-284).
    pose.sneaking = m_intent.sneak && !m_motion.flying;
    pose.sprinting = m_intent.sprint && m_intent.forward > 0.0f && !m_intent.sneak;
    pose.flying = m_motion.flying;
    pose.swingProgress = m_swingTicks < 0 ? -1.0 : (m_swingTicks + alpha) / swingTicks;
    pose.headPitch = m_intent.pitch;
    return pose;
}

QcHandTimers QcClientSession::handTimers(double alpha) const
{
    const auto progress = [alpha](int ticks, int length) { return ticks < 0 ? -1.0 : (ticks + alpha) / length; };
    return {progress(m_switchTicks, switchTicks), progress(m_landingTicks, landingTicks), progress(m_useTicks, useTicks)};
}

QcEntityPose QcClientSession::poseOf(const QcClientEntity &entity, double alpha)
{
    QcEntityPose pose;
    pose.speed = entity.speed;
    pose.walkDistance = entity.walkDistance - entity.lastStep * (1.0 - alpha);
    pose.onGround = (entity.flags & QcMsg::OnGround) != 0;
    pose.inWater = (entity.flags & QcMsg::InWater) != 0;
    pose.sneaking = (entity.flags & QcMsg::Sneaking) != 0;
    pose.sprinting = (entity.flags & QcMsg::Sprinting) != 0;
    pose.swingProgress = entity.swingTicks < 0 ? -1.0 : (entity.swingTicks + alpha) / swingTicks;
    pose.hurtProgress = entity.hurtTicks > 0 ? 1.0 - (entity.hurtTicks - alpha) / QcClientEntities::hurtFlashTicks : -1.0;
    pose.deathProgress = entity.deathTicks < 0 ? -1.0 : std::min(1.0, (entity.deathTicks + alpha) / deathTicks);
    pose.headPitch = entity.previousPitch + (entity.pitch - entity.previousPitch) * float(alpha);
    return pose;
}

bool QcClientSession::dropItem(bool wholeStack)
{
    if (m_state != State::Playing || m_health.dead || m_inventory.selectedStack().isEmpty()) {
        return false;
    }
    m_connection->send(QcMsg::DropItem{wholeStack});
    return true;
}

void QcClientSession::respawn()
{
    if (m_health.dead) {
        m_connection->send(QcMsg::Respawn{});
    }
}

void QcClientSession::updateTargetAndMining()
{
    refreshTarget();

    const std::optional<QcBlockPos> aimed = m_miningRequested && m_target ? std::optional<QcBlockPos>(m_target->block) : std::nullopt;
    // Creative mode: strikes break at once, a held button again every few ticks (decision E-282);
    // a survival mining that still runs is cancelled below.
    const bool instant = m_gameMode.rules().instantBreak;
    if (const std::optional<QcBlockPos> broken = instant ? m_gameMode.nextInstantBreak(aimed) : std::nullopt) {
        m_connection->send(QcMsg::StartMining{*broken});
        m_connection->send(QcMsg::FinishMining{*broken});
        m_swingTicks = 0;
    }

    const std::optional<QcBlockPos> wanted = instant ? std::nullopt : aimed;
    if (wanted != m_miningPos) {
        if (m_miningPos) {
            m_connection->send(QcMsg::CancelMining{});
        }
        m_miningPos = wanted;
        m_miningProgress = 0.0;
        m_miningTicks = 0;
        if (m_miningPos) {
            m_connection->send(QcMsg::StartMining{*m_miningPos});
        }
    }

    if (!m_miningPos) {
        return;
    }

    const QcBlockPos mining = *m_miningPos;
    const std::optional<QcBlockId> block = m_world->blockAt(mining);
    const QcItemStack &held = m_inventory.selectedStack();
    const QcItemDefinition *item = !held.isEmpty() && m_content->isValidItem(held.item) ? &m_content->item(held.item) : nullptr;

    if (!block) {
        return;
    }

    const QcBlockDefinition &definition = m_content->block(*block);
    const double seconds = QcMining::breakSeconds(definition, item);
    if (seconds <= 0.0) {
        return;
    }

    if (m_miningTicks++ % miningHitTicks == 0) {
        cue({QcSoundKeys::blockSound(definition, QcBlockSoundAction::Hit),
             {mining.x + 0.5, mining.y + 0.5, mining.z + 0.5}, 0.35f, 1.0f, QcSoundCue::Origin::Local, 0});
    }

    m_miningProgress += QcPhysics::tickDuration / seconds;
    if (m_miningProgress >= 1.0) {
        // The server decides; its BlockUpdate removes the block (or restores it on rejection).
        m_connection->send(QcMsg::FinishMining{mining});
        m_miningPos.reset();
        m_miningProgress = 0.0;
    }
}

bool QcClientSession::useItem()
{
    if (m_state != State::Playing || m_health.dead) {
        return false;
    }

    const QcItemStack &held = m_inventory.selectedStack();
    const QcItemDefinition *item = !held.isEmpty() && m_content->isValidItem(held.item) ? &m_content->item(held.item) : nullptr;

    // Machines, chests, doors, levers, buttons, repeaters and crafting tables react when used (unless sneaking);
    // otherwise food without a block to place is eaten and everything else acts on the targeted block.
    const std::optional<QcBlockId> targetBlock = m_target ? m_world->blockAt(m_target->block) : std::nullopt;
    const QcBlockDefinition *targetDefinition = targetBlock ? &m_content->block(*targetBlock) : nullptr;
    const bool opensScreen = targetDefinition && !m_intent.sneak
                             && (targetDefinition->opensCrafting || targetDefinition->machine.isMachine()
                                 || targetDefinition->containerSlots > 0 || targetDefinition->toggles
                                 || targetDefinition->signal.isUsable());
    // An armour stand takes or gives armour and items, also with an empty hand.
    if (m_targetEntity && m_entities.all().value(*m_targetEntity).isArmourStand()) {
        m_connection->send(QcMsg::UseEntity{*m_targetEntity});
        m_useTicks = 0;
        return true;
    }

    // Food, armour and what a creature takes (its catching item, its breeding food) used on a creature
    // go to the server, which feeds or catches it or else eats the food or puts on the armour
    // (decisions E-057, E-293); other items act on the block behind it.
    const auto creature = m_targetEntity ? m_entities.all().constFind(*m_targetEntity) : m_entities.all().constEnd();
    if (item && creature != m_entities.all().constEnd() && usesOnCreature(*m_content, *creature, *item)) {
        m_connection->send(QcMsg::UseEntity{creature.key()});
        m_useTicks = 0;
        return true;
    }

    // Food is eaten and armour put on (both sent as ConsumeItem) unless the food plants a block and
    // a block is aimed at: then the server plants it or, where it cannot, eats it (decision E-292).
    const bool eats = item && item->food > 0 && (item->placesBlock.isEmpty() || !m_target);
    if (item && (eats || item->armour) && !opensScreen) {
        m_connection->send(QcMsg::ConsumeItem{});
        m_useTicks = 0;
        return true;
    }

    if (!m_target) {
        return false;
    }

    m_machine.usedBlock();

    const QcVec3 hit = m_target->point - QcVec3{double(m_target->block.x), double(m_target->block.y), double(m_target->block.z)};
    const auto clamp01 = [](double value) { return std::clamp(value, 0.0, 1.0); };
    m_connection->send(QcMsg::UseItem{m_target->block, m_target->face, {clamp01(hit.x), clamp01(hit.y), clamp01(hit.z)}});
    m_useTicks = 0;
    return true;
}

void QcClientSession::takeCraftingResult()
{
    m_connection->send(QcMsg::TakeCraftingResult{});
}

void QcClientSession::craftRecipe(const QString &recipe, int count)
{
    m_connection->send(QcMsg::CraftRecipe{recipe, count});
}

void QcClientSession::closeCrafting()
{
    m_connection->send(QcMsg::CloseCrafting{});
}

void QcClientSession::closeMachine()
{
    if (!m_machine.closeLocally()) {
        return;
    }

    m_connection->send(QcMsg::CloseMachine{});
    emit machineClosed();
}

void QcClientSession::selectHotbarSlot(int slot)
{
    m_inventory.setSelectedHotbarSlot(slot);
    m_connection->send(QcMsg::SelectHotbarSlot{m_inventory.selectedHotbarSlot()});
    emit inventoryChanged();
}

void QcClientSession::moveInventoryItem(int from, int to, int count)
{
    m_connection->send(QcMsg::MoveInventoryItem{from, to, count});
}

void QcClientSession::quickMoveItem(int slot)
{
    m_connection->send(QcMsg::QuickMoveItem{slot});
}

void QcClientSession::sendChat(const QString &text)
{
    if (!text.trimmed().isEmpty()) {
        m_connection->send(QcMsg::ChatInput{text.trimmed()});
    }
}

void QcClientSession::setDebugSubscription(quint8 flags)
{
    const quint8 before = debugSubscription();
    m_debugFlags = flags;
    sendDebugSubscription(before);
}

void QcClientSession::setDebugSubscriptionBase(quint8 flags)
{
    const quint8 before = debugSubscription();
    m_debugBaseFlags = flags;
    sendDebugSubscription(before);
}

void QcClientSession::sendDebugSubscription(quint8 before)
{
    const quint8 flags = debugSubscription();
    if (flags == before) {
        return;
    }

    if (flags == 0) {
        m_serverDebug.clear();
    }
    // Before the welcome the server expects nothing but Hello; onWelcome() sends it then.
    if (m_state == State::Loading || m_state == State::Playing) {
        m_connection->send(QcMsg::DebugSubscribe{flags});
    }
}
