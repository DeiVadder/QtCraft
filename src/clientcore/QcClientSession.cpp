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
    m_connection->send(QcMsg::Hello{QcProtocol::version, m_playerName});
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
        emit inventoryChanged();
    } else if (auto *chat = std::get_if<QcMsg::ChatMessage>(&message)) {
        emit chatReceived(chat->text);
    } else if (auto *spawn = std::get_if<QcMsg::EntitySpawn>(&message)) {
        if (spawn->mobType < -1 || (spawn->mobType >= 0 && (!m_content || spawn->mobType >= m_content->mobs().size()))) {
            m_disconnectReason = u"Server sent an unknown mob type"_s;
            m_connection->close();
            return;
        }
        QcClientEntity entity;
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
            it->position = move.position;
            it->yaw = move.yaw;
            it->health = move.health;
            if (move.hurt) {
                it->hurtTicks = hurtFlashTicks;
            }
        }
    } else if (auto *removed = std::get_if<QcMsg::EntityRemove>(&message)) {
        m_entities.remove(removed->id);
    } else if (auto *health = std::get_if<QcMsg::PlayerHealth>(&message)) {
        m_health = health->health;
        m_maxHealth = health->maxHealth;
        m_dead = health->dead;
        if (m_dead) {
            m_miningRequested = false;
        }
        emit healthChanged();
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
    if (m_state != State::Playing || !m_target) {
        return false;
    }
    m_connection->send(QcMsg::UseItem{m_target->block, m_target->face});
    return true;
}

void QcClientSession::selectHotbarSlot(int slot)
{
    m_inventory.setSelectedHotbarSlot(slot);
    m_connection->send(QcMsg::SelectHotbarSlot{m_inventory.selectedHotbarSlot()});
    emit inventoryChanged();
}

void QcClientSession::moveInventoryItem(int from, int to)
{
    m_connection->send(QcMsg::MoveInventoryItem{from, to});
}

void QcClientSession::sendChat(const QString &text)
{
    if (!text.trimmed().isEmpty()) {
        m_connection->send(QcMsg::ChatInput{text.trimmed()});
    }
}
