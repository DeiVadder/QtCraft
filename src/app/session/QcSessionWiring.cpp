#include "app/session/QcSessionWiring.h"

#include "app/controllers/QcHudState.h"
#include "app/controllers/QcPlayerController.h"
#include "app/controllers/QcScreenState.h"
#include "app/controllers/QcStonecutterController.h"
#include "app/controllers/QcTradeController.h"
#include "app/debug/QcDebugHud.h"
#include "app/hooks/QcStartupHooks.h"
#include "app/session/QcSessionParts.h"
#include "app/session/QcWorldAtlas.h"
#include "audio/QcAudioEngine.h"
#include "clientcore/meshing/QcMeshScheduler.h"
#include "common/physics/QcPhysics.h"

QcSessionWiring::QcSessionWiring(QcSessionParts &parts, const Targets &targets)
    : m_parts(parts)
    , m_targets(targets)
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
        m_targets.audio.setAmbience(m_parts.session->sounds().ambience());
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
    connect(session, &QcClientSession::craftingOpened, &m_targets.screens, &QcScreenState::craftingOpened);
    connect(session, &QcClientSession::foodChanged, player, &QcPlayerController::foodChanged);
    connect(session, &QcClientSession::experienceChanged, player, &QcPlayerController::experienceChanged);
    connect(session, &QcClientSession::gameModeChanged, player, &QcPlayerController::gameModeChanged);
    connect(session, &QcClientSession::actionRefused, player, &QcPlayerController::actionRefused);
    connect(session, &QcClientSession::gameModeChanged, this, [this, session] { m_targets.screens.gameModeChanged(session->gameMode().rules()); });
    connect(session, &QcClientSession::machineOpened, this, &QcSessionWiring::onMachineOpened);
    connect(session, &QcClientSession::machineChanged, player, &QcPlayerController::updateMachine);
    connect(session, &QcClientSession::machineClosed, this, &QcSessionWiring::onMachineClosed);
    connect(&session->trading(), &QcClientTrading::opened, this, &QcSessionWiring::onTradeOpened);
    connect(&session->trading(), &QcClientTrading::changed, trade, &QcTradeController::update);
    connect(&session->trading(), &QcClientTrading::closed, this, &QcSessionWiring::onTradeClosed);
    connect(session, &QcClientSession::inventoryChanged, trade, &QcTradeController::update);
    connect(&session->signEditor(), &QcClientSignEditor::opened, &m_targets.screens, &QcScreenState::signOpened);
    connectStonecutter();
    // On death the screens close before the health properties notify QML.
    connect(session, &QcClientSession::healthChanged, this, [this] { m_targets.screens.healthChanged(m_parts.session->isDead()); });
    connect(session, &QcClientSession::healthChanged, player, &QcPlayerController::healthChanged);
}

void QcSessionWiring::connectStonecutter()
{
    const QcClientSession *session = m_parts.session.get();
    const QcStonecutterController *stonecutter = &m_targets.stonecutter;
    connect(&session->stonecutter(), &QcClientStonecutter::opened, &m_targets.screens, &QcScreenState::stonecutterOpened);
    connect(&session->stonecutter(), &QcClientStonecutter::closed, &m_targets.screens, &QcScreenState::stonecutterClosed);
    connect(&session->stonecutter(), &QcClientStonecutter::opened, stonecutter, &QcStonecutterController::update);
    connect(&session->stonecutter(), &QcClientStonecutter::closed, stonecutter, &QcStonecutterController::update);
    connect(session, &QcClientSession::inventoryChanged, stonecutter, &QcStonecutterController::update);
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
    m_targets.screens.machineOpened(m_parts.session->machine().isContainer());
    m_targets.player.updateMachine();
    m_targets.startup.machineOpened();
}

void QcSessionWiring::onMachineClosed()
{
    m_targets.screens.machineClosed();
    m_targets.player.updateMachine();
}

void QcSessionWiring::onTradeOpened()
{
    m_targets.trade.reset();
    m_targets.screens.tradeOpened();
    m_targets.startup.tradeOpened();
}

void QcSessionWiring::onTradeClosed()
{
    m_targets.screens.tradeClosed();
    m_targets.trade.update();
}
