#include "clientcore/session/QcClientSession.h"

#include "clientcore/debug/QcClientLogging.h"
#include "clientcore/tick/QcSessionLink.h"
#include "common/diagnostics/QcBuildInfo.h"
#include "common/physics/QcPhysics.h"
#include "common/protocol/QcChatText.h"
#include "common/protocol/QcProtocol.h"
#include "common/protocol/QcTransport.h"


#include <QCoreApplication>

#include <cmath>

using namespace Qt::StringLiterals;

QcClientSession::QcClientSession(std::unique_ptr<QcTransport> transport, QString playerName, QObject *parent)
    : QObject(parent)
    , m_playerName(std::move(playerName))
    , m_link(std::make_unique<QcSessionLink>(std::move(transport), [this](QcSessionEvent &&event) { onEvent(std::move(event)); }))
{
    const auto send = [this](const QcMessage &message) { sendToServer(message); };
    m_gameMode.setSender(send);
    m_gameMode.setWishListener([this](bool wanted) { m_link->command({QcSessionCommand::SetFlight{wanted}}); });
    m_debug.setSender(send);
    m_trading.setSender(send);
    m_signEditor.setSender(send);
    m_stonecutter.setSender(send);
}

QcClientSession::~QcClientSession()
{
    // The core may still call back while it stops; the parts it calls must still be there.
    m_link.reset();
}

void QcClientSession::start(TickMode mode)
{
    const bool timer = mode != TickMode::Manual;
    const bool threaded = mode == TickMode::Threaded && QcSessionLink::threadWanted();
    const QcMsg::Hello hello{QcProtocol::version, m_playerName, m_appearance, QcBuildInfo::clientText(QCoreApplication::applicationName()).left(QcMsg::Hello::maxClientLength)};
    m_link->start({hello, timer}, threaded);
    m_helloSent = true;
}

QcTickTiming &QcClientSession::tickTiming() const
{
    return m_link->timing();
}

QcLinkStats &QcClientSession::linkStats() const
{
    return m_link->linkStats();
}

void QcClientSession::setIntent(const QcPlayerInput &intent)
{
    m_prediction.setIntent(intent);
    m_link->command({QcSessionCommand::SetIntent{m_prediction.intent()}});
    refreshTarget();
}

void QcClientSession::resetCorrections()
{
    m_prediction.resetCorrections();
    m_link->command({QcSessionCommand::ResetCorrections{}});
}

void QcClientSession::disconnectFromServer()
{
    if (m_state == State::Disconnected) {
        return;
    }

    sendToServer(QcMsg::Disconnect{u"Client left"_s, {}, {}});
    m_link->command({QcSessionCommand::Close{}});
}

void QcClientSession::setState(State state)
{
    if (m_state == state) {
        return;
    }

    m_state = state;
    emit stateChanged(state);
}

void QcClientSession::onEvent(QcSessionEvent &&event)
{
    if (auto *received = std::get_if<QcSessionEvent::Received>(&event.what)) {
        handleMessage(std::move(received->message));
    } else if (const auto *ticked = std::get_if<QcSessionEvent::Ticked>(&event.what)) {
        onTicked(*ticked);
    } else if (const auto *reconciled = std::get_if<QcSessionEvent::Reconciled>(&event.what)) {
        onReconciled(*reconciled);
    } else if (const auto *closed = std::get_if<QcSessionEvent::Closed>(&event.what)) {
        onClosed(closed->error);
    }
}

void QcClientSession::onClosed(const QString &error)
{
    m_debug.closed();
    if (m_disconnectReason.isEmpty()) {
        m_disconnectReason = error.isEmpty() ? u"Connection closed"_s : error;
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
        && !handleEntityMessage(message) && !handleMachineMessage(message) && !m_trading.handle(message)
        && !m_signEditor.handle(message) && !m_stonecutter.handle(message)) {
        qCWarning(lcSession) << "Ignoring unexpected message type" << message.index();
    }
}

void QcClientSession::closeWithError(const QString &reason)
{
    m_disconnectReason = reason;
    m_link->command({QcSessionCommand::Close{}});
}

bool QcClientSession::handleConnectionMessage(const QcMessage &message)
{
    if (const auto *welcome = std::get_if<QcMsg::Welcome>(&message)) {
        onWelcome(*welcome);
    } else if (const auto *disconnect = std::get_if<QcMsg::Disconnect>(&message)) {
        m_disconnectMessage = *disconnect;
        closeWithError(QcChatText::display(*disconnect));
    } else if (const auto *challenge = std::get_if<QcMsg::LoginChallenge>(&message)) {
        sendToServer(m_login.answer(*challenge));
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
    if (const auto *debug = std::get_if<QcMsg::ServerDebug>(&message)) {
        m_debug.receive(*debug);
        return true;
    }
    if (const auto *sound = std::get_if<QcMsg::SoundEvent>(&message)) {
        cue({sound->key, sound->position, sound->volume, sound->pitch, QcSoundCue::Origin::Server, m_tickCount});
        return true;
    }

    // Only a lightning strike needs the listener (its thunder).
    const bool lightning = std::holds_alternative<QcMsg::Lightning>(message);
    const QcVec3 eye = lightning ? playerMotion().position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0} : QcVec3{};
    const std::optional<QcWorldMirror::Change> change = m_worldMirror.handle(message, eye, m_tickCount);
    if (!change) {
        return false;
    }

    if (!change->error.isEmpty()) {
        closeWithError(change->error);
    } else if (change->contentReady) {
        emit contentReady();
    } else if (change->loaded) {
        emit chunkLoaded(*change->loaded);
    } else if (change->unloaded) {
        emit chunkUnloaded(*change->unloaded);
    }
    for (const auto &[pos, previous] : change->blocks) {
        emit blockChanged(pos, previous);
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
            m_gameMode.mirrorFlight(false);
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
    if (const auto *inventory = std::get_if<QcMsg::InventoryState>(&message)) {
        if (!m_inventory.apply(*inventory, m_worldMirror.content().get())) {
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
        m_gameMode.setAdventure(mode->adventure);
        emit gameModeChanged();
    } else {
        return false;
    }

    return true;
}

bool QcClientSession::handleEntityMessage(const QcMessage &message)
{
    if (const auto *spawn = std::get_if<QcMsg::EntitySpawn>(&message)) {
        if (!m_entities.spawn(*spawn, m_worldMirror.content().get())) {
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

    m_welcome = welcome;
    if (welcome.restoredView) {
        QcPlayerInput intent = m_prediction.intent();
        intent.yaw = welcome.yaw;
        intent.pitch = welcome.pitch;
        setIntent(intent);
    }

    setState(State::Loading);

    if (m_appearancePending) {
        m_appearancePending = false;
        sendToServer(QcMsg::SetAppearance{m_appearance});
    }
    m_debug.welcomed();
}

void QcClientSession::onReconciled(const QcSessionEvent::Reconciled &reconciled)
{
    if (m_state == State::Disconnected) {
        return;
    }

    adopt(reconciled.after);
    if (m_state == State::Loading && m_worldMirror.world()) {
        setState(State::Playing);
    }
}

void QcClientSession::adopt(const QcSessionEvent::Prediction &prediction)
{
    // Until the core has applied the session's latest wish to fly and reset of the corrections, the
    // session's own values are newer than the core's (other commands do not hold them back).
    const bool current = prediction.mirroredApplied >= m_link->mirroredGiven();
    m_prediction.mirror(prediction.prediction, current);
    if (current) {
        m_gameMode.mirrorFlight(prediction.flightWanted);
    }
}

void QcClientSession::tick()
{
    m_link->tick();
}

void QcClientSession::onTicked(const QcSessionEvent::Ticked &event)
{
    if (m_state != State::Playing || !m_worldMirror.world()) {
        adopt(event.after);
        return;
    }

    ++m_tickCount;
    m_worldMirror.tick(m_tickCount);
    m_entities.tick();

    // A hotbar selection the server dropped is sent again (decision E-582).
    m_inventory.resendLostSelection();
    m_animation.tick(m_inventory.heldItem(), m_hand.isMining());
    m_hand.tickLauncher(*m_worldMirror.content(), m_worldMirror.timeSpeed());
    updateSounds();
    adopt(event.after);
    if (!event.predicted) {
        // Nothing to simulate until the server respawns the player.
        emit ticked();
        return;
    }

    if (m_prediction.riding().isRiding()) {
        m_prediction.followCart(m_entities);
    } else {
        const QcEntityMotion &motion = m_prediction.motion();
        const QcEntityMotion &previous = m_prediction.previousMotion();
        m_animation.stepped(previous, motion);
        const QList<QcSoundCue> steps = m_sounds.local().update(previous, motion, m_prediction.intent().sneak, *m_worldMirror.world());
        for (const QcSoundCue &step : steps) {
            cue(step);
        }
    }

    apply(m_hand.updateMining(*m_worldMirror.world(), *m_worldMirror.content()));
    emit ticked();
}

void QcClientSession::cue(QcSoundCue cue)
{
    if (const std::optional<QcSoundCue> recorded = m_sounds.record(std::move(cue), m_tickCount)) {
        emit soundCue(*recorded);
    }
}

void QcClientSession::updateSounds()
{
    m_entities.stepSounds(*m_worldMirror.world(), [this](const QcSoundCue &step) { cue(step); });
    const QcVec3 eye = playerMotion().position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
    const double rain = m_worldMirror.weather().rainAt(int(std::floor(eye.y)));
    m_sounds.updateAmbience(*m_worldMirror.world(), eye, rain, m_tickCount, [this](const QcSoundCue &sound) { cue(sound); });
    const QList<QcSoundCue> thunder = m_worldMirror.weather().tick(m_tickCount);
    for (const QcSoundCue &sound : thunder) {
        cue(sound);
    }
}

void QcClientSession::refreshTarget()
{
    if (m_worldMirror.world()) {
        m_hand.refreshTarget(*m_worldMirror.world(), *m_worldMirror.content());
    } else {
        m_hand.clearTarget();
    }
}

bool QcClientSession::useItem()
{
    if (m_state != State::Playing || m_vitals.isDead()) {
        return false;
    }
    return apply(m_hand.use(*m_worldMirror.world(), *m_worldMirror.content()));
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
    m_link->command({QcSessionCommand::Send{message}});
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
    case HandEffect::Refused:
        if (m_tickCount >= m_nextRefusalReport) {
            m_nextRefusalReport = m_tickCount + refusalReportTicks;
            emit actionRefused();
        }
        return false;
    }
    return true;
}

void QcClientSession::setAppearance(const QcAppearance &appearance)
{
    m_appearance = appearance;
    if (m_state == State::Playing || m_state == State::Loading) {
        sendToServer(QcMsg::SetAppearance{appearance});
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
    sendToServer(QcMsg::DropItem{wholeStack});
    return true;
}

void QcClientSession::respawn()
{
    if (m_vitals.isDead()) {
        sendToServer(QcMsg::Respawn{});
    }
}

void QcClientSession::sendChat(const QString &text)
{
    if (!text.trimmed().isEmpty()) {
        sendToServer(QcMsg::ChatInput{text.trimmed()});
    }
}

bool QcClientSession::handleMachineMessage(const QcMessage &message)
{
    const std::optional<QcClientMachineScreen::Change> change = m_machine.handle(message, m_worldMirror.content().get());
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

    sendToServer(QcMsg::CloseMachine{});
    emit machineClosed();
}

void QcClientSession::selectHotbarSlot(int slot)
{
    m_inventory.selectHotbarSlot(slot);
    emit inventoryChanged();
}
