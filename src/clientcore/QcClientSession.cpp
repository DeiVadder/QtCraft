#include "QcClientSession.h"

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

    m_connection->send(QcMsg::Disconnect{u"Client left"_s});
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
        closeWithError(disconnect->reason);
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
        if (m_world) {
            m_world->removeChunk(unload->pos);
            emit chunkUnloaded(unload->pos);
        }
    } else if (const auto *update = std::get_if<QcMsg::BlockUpdate>(&message)) {
        const QcBlockId previous = m_world ? m_world->blockAt(update->pos).value_or(qcAirBlockId) : qcAirBlockId;
        if (m_world && m_content->isValidBlock(update->block) && m_world->setBlock(update->pos, update->block)) {
            emit blockChanged(update->pos, previous);
        }
    } else if (const auto *sound = std::get_if<QcMsg::SoundEvent>(&message)) {
        cue({sound->key, sound->position, sound->volume, sound->pitch, QcSoundCue::Origin::Server, m_tickCount});
    } else if (const auto *time = std::get_if<QcMsg::WorldTime>(&message)) {
        m_worldTime = time->time;
        m_dayLength = time->dayLength;
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
        emit chatReceived(chat->text);
    } else if (const auto *health = std::get_if<QcMsg::PlayerHealth>(&message)) {
        m_health = health->health;
        m_maxHealth = health->maxHealth;
        m_dead = health->dead;
        if (m_dead) {
            m_miningRequested = false;
        }
        emit healthChanged();
    } else if (const auto *food = std::get_if<QcMsg::PlayerFood>(&message)) {
        m_food = food->food;
        m_maxFood = food->maxFood;
        emit foodChanged();
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
        for (const QcMsg::EntityMove &move : std::as_const(moves->moves)) {
            m_entities.move(move);
        }
    } else if (const auto *removed = std::get_if<QcMsg::EntityRemove>(&message)) {
        m_entities.remove(*removed);
    } else {
        return false;
    }
    return true;
}

bool QcClientSession::handleMachineMessage(const QcMessage &message)
{
    if (const auto *open = std::get_if<QcMsg::OpenMachine>(&message)) {
        onOpenMachine(*open);
    } else if (const auto *machine = std::get_if<QcMsg::MachineState>(&message)) {
        onMachineState(*machine);
    } else if (std::holds_alternative<QcMsg::CloseMachine>(message)) {
        if (m_machine) {
            m_machine.reset();
            emit machineClosed();
        }
    } else {
        return false;
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
    setState(State::Loading);

    if (m_appearancePending) {
        m_appearancePending = false;
        m_connection->send(QcMsg::SetAppearance{m_appearance});
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

    emit inventoryChanged();
}

void QcClientSession::onOpenMachine(const QcMsg::OpenMachine &open)
{
    // An answer to a use that the player already closed again; the server has closed it too.
    if (m_machineClosedLocally) {
        return;
    }

    m_machine = open.pos;
    m_machineCategory = open.category;
    m_machineSlots = QList<QcItemStack>(open.category.isEmpty() ? 0 : QcMsg::MachineState::slotCount);
    m_machineBurn = 0.0;
    m_machineProgress = 0.0;
    emit machineOpened();
}

void QcClientSession::onMachineState(const QcMsg::MachineState &machine)
{
    // Only the open machine's state is shown; unknown items would break the screen.
    const bool known = std::ranges::all_of(machine.stacks, [this](QcItemStack stack) {
        return stack.isEmpty() || (m_content && m_content->isValidItem(stack.item));
    });

    if (m_machine == machine.pos && known) {
        m_machineSlots = machine.stacks;
        m_machineBurn = machine.burn;
        m_machineProgress = machine.progress;
        emit machineChanged();
    }
}

void QcClientSession::reconcile(const QcMsg::PlayerState &state)
{
    // A state older than one already applied would roll the prediction back; ignore it.
    if (m_hasServerState && state.lastInputSequence < m_lastAcknowledged) {
        return;
    }

    m_lastAcknowledged = state.lastInputSequence;
    m_hasServerState = true;
    m_pendingInputs.removeIf([&state](const QcPlayerInput &input) { return input.sequence <= state.lastInputSequence; });

    QcEntityMotion replayed = state.motion;
    if (m_world) {
        for (const QcPlayerInput &input : std::as_const(m_pendingInputs)) {
            QcPhysics::stepPlayer(replayed, input, *m_world, QcPhysics::tickDuration);
        }
    }
    m_motion = replayed;
}

void QcClientSession::tick()
{
    if (m_state != State::Playing || !m_world) {
        return;
    }

    ++m_tickCount;
    m_worldTime += QcPhysics::tickDuration;
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

    if (m_dead) {
        // Nothing to simulate until the server respawns the player.
        emit ticked();
        return;
    }

    QcPlayerInput input = m_intent;
    input.sequence = m_nextSequence++;
    m_previousMotion = m_motion;
    QcPhysics::stepPlayer(m_motion, input, *m_world, QcPhysics::tickDuration);
    m_lastStep = std::hypot(m_motion.position.x - m_previousMotion.position.x, m_motion.position.z - m_previousMotion.position.z);
    m_walkDistance += m_lastStep;

    if (m_motion.onGround && !m_previousMotion.onGround) {
        m_landingTicks = 0;
    }

    const QList<QcSoundCue> steps = m_localSounds.update(m_previousMotion, m_motion, m_intent.sneak, *m_world);
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
    if (cue.key.isEmpty()) {
        return;
    }

    cue.tick = m_tickCount;
    if (m_recentSounds.size() >= recentSoundLimit) {
        m_recentSounds.removeFirst();
    }

    m_recentSounds.append(cue);
    emit soundCue(cue);
}

void QcClientSession::updateSounds()
{
    m_entities.stepSounds(*m_world, [this](const QcSoundCue &step) { cue(step); });

    if (m_tickCount % ambienceTicks == 1) {
        const QcVec3 eye = m_motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
        m_ambienceTarget = QcAmbience::targets(*m_world, eye);

        // A fixed pseudo-random sequence over the samples keeps sessions reproducible.
        const quint64 sample = m_tickCount / ambienceTicks;
        const double roll = double((sample * 2654435761ULL) % 1000ULL) / 1000.0;
        if (roll < QcAmbience::lavaPopChance) {
            if (const std::optional<QcBlockPos> lava = QcAmbience::nearestGlowingLiquid(*m_world, eye)) {
                cue({QString::fromLatin1(QcSoundKeys::lavaPop), {lava->x + 0.5, lava->y + 1.0, lava->z + 0.5}, 1.0f, 1.0f,
                     QcSoundCue::Origin::Local, 0});
            }
        }
    }

    m_ambience = QcAmbience::approach(m_ambience, m_ambienceTarget, QcPhysics::tickDuration);
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
    const bool scoops = !held.isEmpty() && m_content->isValidItem(held.item) && m_content->item(held.item).scoops;
    m_target = QcRaycast::cast(*m_world, eye, direction, QcRaycast::playerReach,
                               scoops ? QcRaycast::Targets::VisibleAndLiquids : QcRaycast::Targets::Visible);

    m_targetEntity.reset();
    double nearest = m_target ? m_target->distance : attackReach;
    for (const QcClientEntity &entity : m_entities.all()) {
        if (entity.isItem() || entity.isFallingBlock()) {
            continue;
        }

        const bool mob = entity.mobType >= 0 && entity.mobType < m_content->mobs().size();
        const double width = mob ? m_content->mobs()[entity.mobType].width : QcPhysics::playerWidth;
        const double height = mob ? m_content->mobs()[entity.mobType].height : QcPhysics::playerHeight;
        const std::optional<double> distance =
            QcRaycast::intersectBox(eye, direction, QcPhysics::boxAt(entity.position, width, height), std::min(nearest, attackReach));

        if (distance && *distance < nearest) {
            nearest = *distance;
            m_targetEntity = entity.id;
        }
    }
}

bool QcClientSession::attack()
{
    if (m_state != State::Playing || m_dead || !m_targetEntity) {
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
    pose.sneaking = m_intent.sneak;
    pose.sprinting = m_intent.sprint && m_intent.forward > 0.0f && !m_intent.sneak;
    pose.swingProgress = m_swingTicks < 0 ? -1.0 : (m_swingTicks + alpha) / swingTicks;
    pose.headPitch = m_intent.pitch;
    return pose;
}

QcClientSession::HandTimers QcClientSession::handTimers(double alpha) const
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
    pose.hurtProgress = entity.hurtTicks > 0 ? 1.0 - (entity.hurtTicks - alpha) / hurtFlashTicks : -1.0;
    pose.deathProgress = entity.deathTicks < 0 ? -1.0 : std::min(1.0, (entity.deathTicks + alpha) / deathTicks);
    pose.headPitch = entity.previousPitch + (entity.pitch - entity.previousPitch) * float(alpha);
    return pose;
}

bool QcClientSession::dropItem(bool wholeStack)
{
    if (m_state != State::Playing || m_dead || m_inventory.selectedStack().isEmpty()) {
        return false;
    }
    m_connection->send(QcMsg::DropItem{wholeStack});
    return true;
}

void QcClientSession::respawn()
{
    if (m_dead) {
        m_connection->send(QcMsg::Respawn{});
    }
}

void QcClientSession::updateTargetAndMining()
{
    refreshTarget();

    const std::optional<QcBlockPos> wanted =
        m_miningRequested && m_target ? std::optional<QcBlockPos>(m_target->block) : std::nullopt;
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
    if (m_state != State::Playing || m_dead) {
        return false;
    }

    const QcItemStack &held = m_inventory.selectedStack();
    const QcItemDefinition *item = !held.isEmpty() && m_content->isValidItem(held.item) ? &m_content->item(held.item) : nullptr;

    // Machines, chests, doors and crafting tables react when used (unless sneaking); otherwise food without a block
    // to place is eaten and everything else acts on the targeted block.
    const std::optional<QcBlockId> targetBlock = m_target ? m_world->blockAt(m_target->block) : std::nullopt;
    const QcBlockDefinition *targetDefinition = targetBlock ? &m_content->block(*targetBlock) : nullptr;
    const bool opensScreen = targetDefinition && !m_intent.sneak
                             && (targetDefinition->opensCrafting || targetDefinition->machine.isMachine()
                                 || targetDefinition->containerSlots > 0 || targetDefinition->toggles);
    if (item && item->food > 0 && item->placesBlock.isEmpty() && !opensScreen) {
        m_connection->send(QcMsg::ConsumeItem{});
        m_useTicks = 0;
        return true;
    }

    // A creature caught with the held item (a firefly with an empty bottle).
    if (item && m_targetEntity) {
        const auto entity = m_entities.all().constFind(*m_targetEntity);
        if (entity != m_entities.all().constEnd() && entity->mobType >= 0 && entity->mobType < m_content->mobs().size()
            && m_content->mobs()[entity->mobType].catchWith == item->name) {
            m_connection->send(QcMsg::UseEntity{*m_targetEntity});
            m_useTicks = 0;
            return true;
        }
    }

    if (!m_target) {
        return false;
    }

    m_machineClosedLocally = false;

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
    if (!m_machine) {
        return;
    }

    m_machine.reset();
    m_machineClosedLocally = true;
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

void QcClientSession::sendChat(const QString &text)
{
    if (!text.trimmed().isEmpty()) {
        m_connection->send(QcMsg::ChatInput{text.trimmed()});
    }
}
