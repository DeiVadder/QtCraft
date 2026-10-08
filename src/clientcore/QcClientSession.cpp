#include "QcClientSession.h"

#include "QcConnection.h"
#include "QcMining.h"
#include "QcPhysics.h"
#include "QcProtocol.h"
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
    if (auto *welcome = std::get_if<QcMsg::Welcome>(&message)) {
        if (welcome->protocolVersion != QcProtocol::version) {
            m_disconnectReason = u"Server uses protocol version %1, this client %2"_s.arg(welcome->protocolVersion)
                                     .arg(QcProtocol::version);
            m_connection->close();
            return;
        }
        m_entityId = welcome->playerEntityId;
        if (welcome->restoredView) {
            QcPlayerInput intent = m_intent;
            intent.yaw = welcome->yaw;
            intent.pitch = welcome->pitch;
            setIntent(intent);
        }
        m_worldName = welcome->worldName;
        m_seed = welcome->seed;
        setState(State::Loading);
        if (m_appearancePending) {
            m_appearancePending = false;
            m_connection->send(QcMsg::SetAppearance{m_appearance});
        }
    } else if (auto *sync = std::get_if<QcMsg::ContentSync>(&message)) {
        if (m_content) {
            // Block ids of loaded chunks would no longer match; content changes need a reconnect.
            m_disconnectReason = u"Server sent content twice"_s;
            m_connection->close();
            return;
        }
        m_content = sync->content;
        m_world = std::make_unique<QcClientWorld>(m_content);
        emit contentReady();
    } else if (auto *chunkData = std::get_if<QcMsg::ChunkData>(&message)) {
        if (!m_world || !chunkUsesKnownBlocks(*chunkData->chunk, *m_content)) {
            m_disconnectReason = u"Server sent invalid chunk data"_s;
            m_connection->close();
            return;
        }
        const QcChunkPos pos = chunkData->chunk->pos();
        m_world->setChunk(chunkData->chunk);
        emit chunkLoaded(pos);
    } else if (auto *unload = std::get_if<QcMsg::ChunkUnload>(&message)) {
        if (m_world) {
            m_world->removeChunk(unload->pos);
            emit chunkUnloaded(unload->pos);
        }
    } else if (auto *update = std::get_if<QcMsg::BlockUpdate>(&message)) {
        if (m_world && m_content->isValidBlock(update->block) && m_world->setBlock(update->pos, update->block)) {
            emit blockChanged(update->pos);
        }
    } else if (auto *playerState = std::get_if<QcMsg::PlayerState>(&message)) {
        reconcile(*playerState);
        if (m_state == State::Loading && m_world) {
            setState(State::Playing);
        }
    } else if (auto *inventory = std::get_if<QcMsg::InventoryState>(&message)) {
        if (inventory->stacks.size() != QcInventory::slotCount) {
            m_disconnectReason = u"Server sent an invalid inventory"_s;
            m_connection->close();
            return;
        }
        for (int i = 0; i < QcInventory::slotCount; ++i) {
            m_inventory.setSlot(i, inventory->stacks[i]);
        }
        m_inventory.setSelectedHotbarSlot(inventory->selected);
        if (inventory->craftingGrid.size() == 9 && (inventory->craftingSize == 2 || inventory->craftingSize == 3)) {
            m_craftingGrid = inventory->craftingGrid;
            m_craftingSize = inventory->craftingSize;
            m_craftingResult = inventory->craftingResult;
        }
        emit inventoryChanged();
    } else if (auto *open = std::get_if<QcMsg::OpenCrafting>(&message)) {
        emit craftingOpened(open->size);
    } else if (auto *chat = std::get_if<QcMsg::ChatMessage>(&message)) {
        emit chatReceived(chat->text);
    } else if (auto *spawn = std::get_if<QcMsg::EntitySpawn>(&message)) {
        const bool item = spawn->mobType == QcMsg::EntitySpawn::itemType;
        if ((spawn->mobType < QcMsg::EntitySpawn::playerType && !item)
            || (spawn->mobType >= 0 && (!m_content || spawn->mobType >= m_content->mobs().size()))
            || (item && (!m_content || spawn->item.isEmpty() || !m_content->isValidItem(spawn->item.item)))) {
            m_disconnectReason = u"Server sent an unknown entity type"_s;
            m_connection->close();
            return;
        }
        // A known entity sent again changed its count (items) or look (players); keep its motion history.
        if (const auto known = m_entities.find(spawn->id); known != m_entities.end() && known->mobType == spawn->mobType) {
            known->item = item ? spawn->item : QcItemStack();
            known->appearance = spawn->appearance;
            return;
        }
        QcClientEntity entity;
        entity.item = item ? spawn->item : QcItemStack();
        entity.appearance = spawn->appearance;
        entity.pitch = entity.previousPitch = spawn->pitch;
        entity.flags = spawn->flags;
        entity.held = spawn->held;
        entity.id = spawn->id;
        entity.mobType = spawn->mobType;
        entity.name = spawn->name;
        entity.position = entity.previousPosition = spawn->position;
        entity.yaw = entity.previousYaw = spawn->yaw;
        entity.health = spawn->health;
        m_entities.insert(entity.id, entity);
    } else if (auto *moves = std::get_if<QcMsg::EntityMoves>(&message)) {
        for (const QcMsg::EntityMove &move : std::as_const(moves->moves)) {
            const auto it = m_entities.find(move.id);
            if (it == m_entities.end()) {
                continue;
            }
            it->previousPosition = it->position;
            it->previousYaw = it->yaw;
            it->previousPitch = it->pitch;
            it->lastStep = std::hypot(move.position.x - it->position.x, move.position.z - it->position.z);
            it->walkDistance += it->lastStep;
            it->stepThisTick += it->lastStep;
            it->position = move.position;
            it->yaw = move.yaw;
            it->pitch = move.pitch;
            it->health = move.health;
            it->flags = move.flags;
            it->held = move.held;
            it->moved = true;
            if ((move.flags & QcMsg::Swing) != 0) {
                it->swingTicks = 0;
            }
            if (move.hurt) {
                it->hurtTicks = hurtFlashTicks;
            }
        }
    } else if (auto *removed = std::get_if<QcMsg::EntityRemove>(&message)) {
        QcClientEntity gone = m_entities.take(removed->id);
        if (removed->died && gone.id == removed->id && !gone.isItem()) {
            gone.deathTicks = 0;
            gone.previousPosition = gone.position;
            m_dying.insert(gone.id, gone);
        }
    } else if (auto *health = std::get_if<QcMsg::PlayerHealth>(&message)) {
        m_health = health->health;
        m_maxHealth = health->maxHealth;
        m_dead = health->dead;
        if (m_dead) {
            m_miningRequested = false;
        }
        emit healthChanged();
    } else if (auto *food = std::get_if<QcMsg::PlayerFood>(&message)) {
        m_food = food->food;
        m_maxFood = food->maxFood;
        emit foodChanged();
    } else if (auto *open = std::get_if<QcMsg::OpenMachine>(&message)) {
        // An answer to a use that the player already closed again; the server has closed it too.
        if (m_machineClosedLocally) {
            return;
        }
        m_machine = open->pos;
        m_machineCategory = open->category;
        m_machineSlots = QList<QcItemStack>(QcMsg::MachineState::slotCount);
        m_machineBurn = 0.0;
        m_machineProgress = 0.0;
        emit machineOpened();
    } else if (auto *machine = std::get_if<QcMsg::MachineState>(&message)) {
        // Only the open machine's state is shown; unknown items would break the screen.
        const bool known = std::ranges::all_of(machine->stacks, [this](QcItemStack stack) {
            return stack.isEmpty() || (m_content && m_content->isValidItem(stack.item));
        });
        if (m_machine == machine->pos && known) {
            m_machineSlots = machine->stacks;
            m_machineBurn = machine->burn;
            m_machineProgress = machine->progress;
            emit machineChanged();
        }
    } else if (std::holds_alternative<QcMsg::CloseMachine>(message)) {
        if (m_machine) {
            m_machine.reset();
            emit machineClosed();
        }
    } else if (auto *time = std::get_if<QcMsg::WorldTime>(&message)) {
        m_worldTime = time->time;
        m_dayLength = time->dayLength;
    } else if (auto *disconnect = std::get_if<QcMsg::Disconnect>(&message)) {
        m_disconnectReason = disconnect->reason;
        m_connection->close();
    } else {
        qCWarning(lcSession) << "Ignoring unexpected message type" << message.index();
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
    for (QcClientEntity &entity : m_entities) {
        entity.hurtTicks = std::max(0, entity.hurtTicks - 1);
        entity.speed += (entity.stepThisTick * QcPhysics::tickRate - entity.speed) * speedSmoothing;
        entity.stepThisTick = 0.0;
        // Items move only on change, and a tick may pass without a mob or player move: then the
        // entity stays where it is (no interpolation towards an old position).
        if (!entity.moved) {
            entity.lastStep = 0.0;
            entity.previousPosition = entity.position;
            entity.previousYaw = entity.yaw;
            entity.previousPitch = entity.pitch;
        }
        entity.moved = false;
        if (entity.swingTicks >= 0) {
            ++entity.swingTicks;
            if (entity.swingTicks >= swingTicks) {
                entity.swingTicks = -1;
            }
        }
    }
    for (QcClientEntity &dying : m_dying) {
        ++dying.deathTicks;
    }
    m_dying.removeIf([](const auto &entry) { return entry.value().deathTicks >= deathTicks; });
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
    if (m_pendingInputs.size() >= maxPendingInputs) {
        m_pendingInputs.removeFirst();
    }
    m_pendingInputs.append(input);
    m_connection->send(QcMsg::PlayerInput{input});
    updateTargetAndMining();
    emit ticked();
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
    m_target = QcRaycast::cast(*m_world, eye, direction, QcRaycast::playerReach);

    m_targetEntity.reset();
    double nearest = m_target ? m_target->distance : attackReach;
    for (const QcClientEntity &entity : std::as_const(m_entities)) {
        if (entity.isItem()) {
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
        if (m_miningPos) {
            m_connection->send(QcMsg::StartMining{*m_miningPos});
        }
    }
    if (!m_miningPos) {
        return;
    }
    const std::optional<QcBlockId> block = m_world->blockAt(*m_miningPos);
    const QcItemStack &held = m_inventory.selectedStack();
    const QcItemDefinition *item = !held.isEmpty() && m_content->isValidItem(held.item) ? &m_content->item(held.item) : nullptr;
    const double seconds = block ? QcMining::breakSeconds(m_content->block(*block), item) : -1.0;
    if (seconds <= 0.0) {
        return;
    }
    m_miningProgress += QcPhysics::tickDuration / seconds;
    if (m_miningProgress >= 1.0) {
        // The server decides; its BlockUpdate removes the block (or restores it on rejection).
        m_connection->send(QcMsg::FinishMining{*m_miningPos});
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
    // Machines and crafting tables open when used (unless sneaking); otherwise food without a block
    // to place is eaten and everything else acts on the targeted block.
    const std::optional<QcBlockId> targetBlock = m_target ? m_world->blockAt(m_target->block) : std::nullopt;
    const bool opensScreen = targetBlock && !m_intent.sneak
                             && (m_content->block(*targetBlock).opensCrafting || m_content->block(*targetBlock).machine.isMachine());
    if (item && item->food > 0 && item->placesBlock.isEmpty() && !opensScreen) {
        m_connection->send(QcMsg::ConsumeItem{});
        m_useTicks = 0;
        return true;
    }
    if (!m_target) {
        return false;
    }
    m_machineClosedLocally = false;
    m_connection->send(QcMsg::UseItem{m_target->block, m_target->face});
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
