#include "QcClientSession.h"

#include "QcConnection.h"
#include "QcPhysics.h"
#include "QcProtocol.h"
#include "QcTransport.h"

#include <QLoggingCategory>
#include <QTimer>

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
    QcPlayerInput input = m_intent;
    input.sequence = m_nextSequence++;
    m_previousMotion = m_motion;
    QcPhysics::stepPlayer(m_motion, input, *m_world, QcPhysics::tickDuration);
    if (m_pendingInputs.size() >= maxPendingInputs) {
        m_pendingInputs.removeFirst();
    }
    m_pendingInputs.append(input);
    m_connection->send(QcMsg::PlayerInput{input});
    emit ticked();
}
