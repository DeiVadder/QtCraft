#include "clientcore/session/QcClientSession.h"

#include "common/physics/QcPhysics.h"
#include "common/protocol/QcChatText.h"
#include "common/protocol/QcConnection.h"
#include "common/protocol/QcProtocol.h"
#include "common/protocol/QcTransport.h"

#include <QLoggingCategory>
#include <QTimer>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

Q_LOGGING_CATEGORY(lcSession, "qtcraft.session")

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
    m_debug.setSender([this](const QcMessage &message) { m_connection->send(message); });
    m_trading.setSender([this](const QcMessage &message) { m_connection->send(message); });

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
    m_prediction.setIntent(intent);
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
    m_debug.closed();
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
        && !handleEntityMessage(message) && !handleMachineMessage(message) && !m_trading.handle(message)) {
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
        m_debug.receive(*debug);
    } else if (const auto *update = std::get_if<QcMsg::BlockUpdate>(&message)) {
        const QcBlockId previous = m_world ? m_world->blockAt(update->pos).value_or(qcAirBlockId) : qcAirBlockId;
        if (m_world && m_content->isValidBlock(update->block) && m_world->setBlock(update->pos, update->block)) {
            emit blockChanged(update->pos, previous);
        }
    } else if (const auto *sound = std::get_if<QcMsg::SoundEvent>(&message)) {
        cue({sound->key, sound->position, sound->volume, sound->pitch, QcSoundCue::Origin::Server, m_tickCount});
    } else if (const auto *time = std::get_if<QcMsg::WorldTime>(&message)) {
        m_worldTime = *time;
    } else if (const auto *weather = std::get_if<QcMsg::Weather>(&message)) {
        m_weather.receive(*weather);
    } else if (const auto *strike = std::get_if<QcMsg::Lightning>(&message)) {
        m_weather.lightning(*strike, playerMotion().position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0}, m_tickCount);
    } else {
        return false;
    }

    return true;
}

void QcClientSession::onVitalsChanged(QcClientVitals::Change change)
{
    switch (change) {
    case QcClientVitals::Change::Health:
        if (m_vitals.isDead()) {
            // A player comes back from death on the ground.
            m_hand.setMining(false);
            m_gameMode.setFlightWanted(false);
        }
        emit healthChanged();
        break;
    case QcClientVitals::Change::Food:
        emit foodChanged();
        break;
    case QcClientVitals::Change::Experience:
        emit experienceChanged();
        break;
    case QcClientVitals::Change::None:
        break;
    }
}

bool QcClientSession::handlePlayerMessage(const QcMessage &message)
{
    if (const auto *playerState = std::get_if<QcMsg::PlayerState>(&message)) {
        reconcile(*playerState);
        if (m_state == State::Loading && m_world) {
            setState(State::Playing);
        }
    } else if (const auto *inventory = std::get_if<QcMsg::InventoryState>(&message)) {
        if (!m_inventory.apply(*inventory, m_content.get())) {
            closeWithError(u"Server sent an invalid inventory"_s);
            return true;
        }
        emit inventoryChanged();
    } else if (const auto *open = std::get_if<QcMsg::OpenCrafting>(&message)) {
        emit craftingOpened(open->size);
    } else if (const auto *chat = std::get_if<QcMsg::ChatMessage>(&message)) {
        emit chatReceived(QcChatText::display(*chat));
    } else if (const QcClientVitals::Change change = m_vitals.apply(message); change != QcClientVitals::Change::None) {
        onVitalsChanged(change);
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

void QcClientSession::onWelcome(const QcMsg::Welcome &welcome)
{
    if (welcome.protocolVersion != QcProtocol::version) {
        closeWithError(u"Server uses protocol version %1, this client %2"_s.arg(welcome.protocolVersion).arg(QcProtocol::version));
        return;
    }

    m_entityId = welcome.playerEntityId;
    if (welcome.restoredView) {
        QcPlayerInput intent = m_prediction.intent();
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
    m_debug.welcomed();
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

void QcClientSession::reconcile(const QcMsg::PlayerState &state)
{
    // A player saved while flying flies on after joining (decision E-284).
    if (m_prediction.reconcile(state, m_world.get(), m_gameMode.rules().mayFly) == QcPrediction::Reconciled::Placed) {
        m_gameMode.setFlightWanted(state.motion.flying);
    }
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

    m_animation.tick(m_inventory.heldItem(), m_hand.isMining());
    m_hand.tickLauncher(*m_content, m_worldTime.speed);
    updateSounds();

    if (m_vitals.isDead()) {
        // Nothing to simulate until the server respawns the player.
        emit ticked();
        return;
    }

    predict();
    apply(m_hand.updateMining(*m_world, *m_content));
    emit ticked();
}

void QcClientSession::predict()
{
    const QcPlayerInput input = m_prediction.predict(*m_world, m_entities, m_gameMode.flightWanted(), m_gameMode.rules().mayFly);
    if (!m_prediction.riding().isRiding()) {
        const QcEntityMotion &motion = m_prediction.motion();
        const QcEntityMotion &previous = m_prediction.previousMotion();
        m_gameMode.stepped(motion.flying);
        m_animation.stepped(previous, motion);

        const QList<QcSoundCue> steps = m_sounds.local().update(previous, motion, m_prediction.intent().sneak, *m_world);
        for (const QcSoundCue &step : steps) {
            cue(step);
        }
    }

    m_connection->send(QcMsg::PlayerInput{input});
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
    const QcVec3 eye = playerMotion().position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
    const double rain = m_weather.rainAt(int(std::floor(eye.y)));
    m_sounds.updateAmbience(*m_world, eye, rain, m_tickCount, [this](const QcSoundCue &sound) { cue(sound); });
    const QList<QcSoundCue> thunder = m_weather.tick(m_tickCount);
    for (const QcSoundCue &sound : thunder) {
        cue(sound);
    }
}

void QcClientSession::refreshTarget()
{
    if (m_world) {
        m_hand.refreshTarget(*m_world, *m_content);
    } else {
        m_hand.clearTarget();
    }
}

bool QcClientSession::useItem()
{
    if (m_state != State::Playing || m_vitals.isDead()) {
        return false;
    }
    return apply(m_hand.use(*m_world, *m_content));
}

bool QcClientSession::attack()
{
    if (m_state != State::Playing || m_vitals.isDead()) {
        return false;
    }
    return apply(m_hand.attack());
}

void QcClientSession::sendToServer(const QcMessage &message)
{
    m_connection->send(message);
}

bool QcClientSession::apply(QcHandActions::HandEffect effect)
{
    // The answers to the hand's requests arrive later (the transports deliver queued), so the
    // screens are marked before the server can open one.
    using HandEffect = QcHandActions::HandEffect;
    switch (effect) {
    case HandEffect::None:
        return false;
    case HandEffect::Handled:
        break;
    case HandEffect::Swing:
        m_animation.swing();
        break;
    case HandEffect::UsedEntity:
        m_machine.usedBlock();
        m_trading.usedEntity();
        m_animation.use();
        break;
    case HandEffect::UsedBlock:
        m_machine.usedBlock();
        m_animation.use();
        break;
    case HandEffect::Use:
        m_animation.use();
        break;
    }
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

bool QcClientSession::dropItem(bool wholeStack)
{
    if (m_state != State::Playing || m_vitals.isDead() || inventory().selectedStack().isEmpty()) {
        return false;
    }
    m_connection->send(QcMsg::DropItem{wholeStack});
    return true;
}

void QcClientSession::respawn()
{
    if (m_vitals.isDead()) {
        m_connection->send(QcMsg::Respawn{});
    }
}

void QcClientSession::sendChat(const QString &text)
{
    if (!text.trimmed().isEmpty()) {
        m_connection->send(QcMsg::ChatInput{text.trimmed()});
    }
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
    m_inventory.selectHotbarSlot(slot);
    emit inventoryChanged();
}
