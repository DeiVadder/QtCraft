#include "app/session/QcSessionWiring.h"

#include "app/controllers/QcHudState.h"
#include "app/controllers/QcPlayerController.h"
#include "app/controllers/QcTradeController.h"
#include "app/debug/QcDebugHud.h"
#include "app/hooks/QcStartupHooks.h"
#include "app/input/QcGameInput.h"
#include "app/session/QcSessionParts.h"
#include "app/session/QcWorldAtlas.h"
#include "audio/QcAudioEngine.h"
#include "clientcore/meshing/QcMeshScheduler.h"
#include "common/physics/QcPhysics.h"

using namespace Qt::StringLiterals;

QcSessionWiring::QcSessionWiring(QcSessionParts &parts, const Targets &targets, Overlay overlay)
    : m_parts(parts)
    , m_targets(targets)
    , m_overlay(std::move(overlay))
{
}

void QcSessionWiring::connectSession()
{
    connectWorld();
    connectPlayer();
}

void QcSessionWiring::connectWorld()
{
    const QcClientSession *session = m_parts.session.get();
    connect(session, &QcClientSession::contentReady, this, &QcSessionWiring::onContentReady);
    connect(session, &QcClientSession::chunkLoaded, this, [this](QcChunkPos pos) { m_parts.meshes->chunkLoaded(*m_parts.session->world(), pos); });
    connect(session, &QcClientSession::chunkUnloaded, m_parts.meshes.get(), &QcMeshScheduler::chunkUnloaded);
    connect(session, &QcClientSession::blockChanged, this, [this](QcBlockPos pos, QcBlockId previous) {
        m_parts.meshes->blockChanged(*m_parts.session->world(), pos, previous);
    });
    connect(session, &QcClientSession::ticked, this, [this] {
        emit ticked();
        m_targets.audio.setAmbience(m_parts.session->ambience());
    });
    connect(session, &QcClientSession::soundCue, this, [this](const QcSoundCue &cue) {
        const QcVec3 ears = m_parts.session->playerMotion().position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
        // Left and right as seen on screen: the front view looks back at the player.
        const bool front = m_parts.session->cameraMode() == QcCameraMode::Front;
        m_targets.audio.play(cue, ears, m_parts.session->intent().yaw + (front ? 180.0f : 0.0f));
    });
    connect(session, &QcClientSession::chatReceived, &m_targets.hud, &QcHudState::addChatLine);
    connect(session, &QcClientSession::stateChanged, this, &QcSessionWiring::onSessionState);

    connect(m_parts.meshes.get(), &QcMeshScheduler::meshReady, this, [this](quint64 key, std::shared_ptr<const QcChunkMesh> mesh) {
        m_targets.hud.debug()->meshed(key);
        if (m_parts.view) {
            m_parts.view->submitMesh(key, std::move(mesh));
        }
    });
    connect(m_parts.meshes.get(), &QcMeshScheduler::meshRemoved, this, [this](quint64 key) {
        if (m_parts.view) {
            m_parts.view->removeMesh(key);
        }
    });
}

void QcSessionWiring::connectPlayer()
{
    const QcClientSession *session = m_parts.session.get();
    const QcPlayerController *player = &m_targets.player;
    const QcTradeController *trade = &m_targets.trade;
    connect(session, &QcClientSession::inventoryChanged, player, &QcPlayerController::updateInventory);
    connect(session, &QcClientSession::craftingOpened, this, [this] { m_overlay.set(u"inventory"_s); });
    connect(session, &QcClientSession::foodChanged, player, &QcPlayerController::foodChanged);
    connect(session, &QcClientSession::experienceChanged, player, &QcPlayerController::experienceChanged);
    connect(session, &QcClientSession::gameModeChanged, player, &QcPlayerController::gameModeChanged);
    // An operator changed the mode while the creative inventory was open: the usual one instead.
    connect(session, &QcClientSession::gameModeChanged, this, [this] {
        if (m_overlay.get() == u"creative"_s && !m_targets.player.creative()) {
            m_overlay.set(u"inventory"_s);
        }
    });
    connect(session, &QcClientSession::machineOpened, this, &QcSessionWiring::onMachineOpened);
    connect(session, &QcClientSession::machineChanged, player, &QcPlayerController::updateMachine);
    connect(session, &QcClientSession::machineClosed, this, &QcSessionWiring::onMachineClosed);
    connect(&session->trading(), &QcClientTrading::opened, this, &QcSessionWiring::onTradeOpened);
    connect(&session->trading(), &QcClientTrading::changed, trade, &QcTradeController::update);
    connect(&session->trading(), &QcClientTrading::closed, this, &QcSessionWiring::onTradeClosed);
    connect(session, &QcClientSession::inventoryChanged, trade, &QcTradeController::update);
    // On death the screens close before the health properties notify QML.
    connect(session, &QcClientSession::healthChanged, this, &QcSessionWiring::onHealthChanged);
    connect(session, &QcClientSession::healthChanged, player, &QcPlayerController::healthChanged);
}

void QcSessionWiring::onSessionState(QcClientSession::State state)
{
    if (state == QcClientSession::State::Playing) {
        emit playing();
        m_targets.startup.worldPlaying();
    } else if (state == QcClientSession::State::Disconnected) {
        emit disconnected(m_parts.session->disconnectReason());
    }
}

void QcSessionWiring::onContentReady()
{
    m_targets.atlas.enterWorld();
    m_targets.player.useContent();
    m_targets.audio.setContent(m_parts.session->content());
    m_targets.audio.setMusicEnabled(true);
}

void QcSessionWiring::onMachineOpened()
{
    m_targets.player.nameOpenMachine();
    m_overlay.set(m_parts.session->machine().isContainer() ? u"chest"_s : u"furnace"_s);
    m_targets.player.updateMachine();
    m_targets.startup.machineOpened();
}

void QcSessionWiring::onMachineClosed()
{
    const QString overlay = m_overlay.get();
    if (overlay == u"furnace"_s || overlay == u"chest"_s) {
        m_overlay.set({});
    }
    m_targets.player.updateMachine();
}

void QcSessionWiring::onTradeOpened()
{
    m_targets.trade.reset();
    m_overlay.set(u"trade"_s);
    m_targets.startup.tradeOpened();
}

void QcSessionWiring::onTradeClosed()
{
    if (m_overlay.get() == u"trade"_s) {
        m_overlay.set({});
    }
    m_targets.trade.update();
}

void QcSessionWiring::onHealthChanged()
{
    if (!m_parts.session->isDead()) {
        // Respawned: back into the world with the mouse captured.
        m_targets.input.syncCapture();
        return;
    }

    m_overlay.set({});
    m_targets.input.releaseMouse();
    // Releases on the death screen go to QML; forget held keys so nothing sticks after respawn.
    m_targets.input.clearKeys();
    m_parts.session->setMining(false);
    m_parts.session->cancelUse();
}
