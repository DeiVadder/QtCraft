#include "QcGameController.h"

#include "QcAudioEngine.h"
#include "QcBenchmarkRun.h"
#include "QcBlockRenderTable.h"
#include "QcLocalServerHost.h"
#include "QcMeshScheduler.h"
#include "QcModLoader.h"
#include "QcPhysics.h"
#include "QcTerrainGenerator.h"
#include "QcTextureAtlas.h"
#include "QcWorldLibrary.h"

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QQuickWindow>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

Q_LOGGING_CATEGORY(lcGame, "qtcraft.game")

// Stable across runs and platforms, unlike qHash.
qint64 seedFromText(const QString &text)
{
    bool ok = false;
    const qint64 number = text.trimmed().toLongLong(&ok);
    if (ok) {
        return number;
    }
    quint64 hash = 14695981039346656037ull;
    for (const QChar c : text) {
        hash ^= c.unicode();
        hash *= 1099511628211ull;
    }
    return qint64(hash);
}

QcVec3 lerp(const QcVec3 &a, const QcVec3 &b, double t)
{
    return a + (b - a) * t;
}

} // namespace

QcGameController::QcGameController(QObject *parent)
    : QObject(parent)
    , m_audio(new QcAudioEngine(this))
    , m_library(new QcWorldLibraryController(this))
    , m_settings(new QcSettingsController(m_parts, *m_library, m_audio, this))
    , m_player(new QcPlayerController(m_parts, *m_settings, this))
    , m_hud(new QcHudState(m_parts, *m_settings, this))
    , m_handbook(new QcHandbookController(m_parts, *m_settings, *m_library, m_icons, this))
    , m_input(new QcGameInput(*this, m_parts, *m_settings, m_audio, this))
    , m_benchmark(new QcBenchmarkRun(*this, m_parts, *m_input, *m_settings, this))
    , m_startup(new QcStartupHooks(*this, m_parts, m_benchmark, this))
    , m_render(m_parts, *m_settings)
{
    m_frameClock.start();
    m_fpsClock.start();
    connect(m_handbook, &QcHandbookController::opening, this, [this] {
        m_input->clearKeys();
        if (m_parts.session) {
            m_parts.session->setMining(false);
        }
        m_input->releaseMouse();
    });
    // Closing the handbook gives the world its mouse back without a click.
    connect(m_handbook, &QcHandbookController::openChanged, m_input, &QcGameInput::syncCapture);
    connect(m_library, &QcWorldLibraryController::contentReset, this, &QcGameController::onContentReset);
    connect(m_settings, &QcSettingsController::debugOverlaySwitched, m_hud, &QcHudState::resetDebugClock);
}

QcGameController::~QcGameController()
{
    if (const QcStatus saved = stopSession(); !saved) {
        qCWarning(lcGame) << "Saving the world failed:" << saved.error();
    }

    // The input filter and the parts below hold references to m_parts and m_icons, which die before
    // QObject deletes the children: remove the application-wide filter and delete them first.
    if (QCoreApplication *application = QCoreApplication::instance()) {
        application->removeEventFilter(m_input);
    }
    delete m_startup;
    delete m_benchmark;
    delete m_input;
    delete m_handbook;
    delete m_hud;
}

void QcGameController::setView(QcWorldView *view)
{
    if (m_parts.view == view) {
        return;
    }

    m_parts.view = view;
    if (m_parts.view) {
        if (m_parts.view->window()) {
            attachWindow(m_parts.view->window());
        }
        connect(m_parts.view, &QQuickItem::windowChanged, this, &QcGameController::attachWindow);
    }
    emit viewChanged();
}

void QcGameController::attachWindow(QQuickWindow *window)
{
    if (m_parts.window == window) {
        return;
    }

    if (m_parts.window) {
        disconnect(m_parts.window, nullptr, this, nullptr);
    }
    m_parts.window = window;
    if (m_parts.window) {
        connect(m_parts.window, &QQuickWindow::afterAnimating, this, &QcGameController::onFrame);
        connect(m_parts.window, &QWindow::activeChanged, this, [this] {
            if (!m_parts.window->isActive()) {
                m_input->clearKeys();
                m_input->releaseMouse();
            }
        });
    }
}

void QcGameController::setPhase(const QString &phase, const QString &status)
{
    if (m_statusText != status) {
        m_statusText = status;
        emit statusChanged();
    }
    if (m_phase != phase) {
        m_phase = phase;
        emit phaseChanged();
        m_input->syncCapture();
    }
}

void QcGameController::setInputEnabled(bool enabled)
{
    if (m_inputEnabled == enabled) {
        return;
    }

    m_inputEnabled = enabled;
    if (!enabled) {
        m_input->clearKeys();
        m_input->releaseMouse();
    }
    emit inputEnabledChanged();
    m_input->syncCapture();
}

void QcGameController::startAudio(bool enabled)
{
    m_audio->start(enabled);
    m_settings->audio()->apply();
    if (m_audio->isActive() && !m_parts.session) {
        // Menu clicks come from the content as well; the base game and mods are loaded once here.
        m_library->content();
        m_audio->setContent(m_library->loadedContent());
    }
}

void QcGameController::setLanguage(const QString &language)
{
    m_settings->setContentLanguage(language);
    // Item and recipe names come from the content's translations.
    if (m_parts.session && m_parts.session->content()) {
        m_player->useContent();
    }
    m_settings->character()->refreshLists();
    // Key and action names are translated.
    m_settings->keys()->retranslate();
    m_handbook->refreshIfOpen();
}

void QcGameController::startSingleplayer(const QString &worldName, const QString &seedText)
{
    startWorld({}, worldName.trimmed().isEmpty() ? tr("New World") : worldName.trimmed(), seedFromText(seedText));
}

void QcGameController::createWorld(const QString &worldName, const QString &seedText, bool keepInventory)
{
    const QcWorldLibrary library(QcWorldLibrary::defaultRoot());
    const QString name = worldName.trimmed().isEmpty() ? tr("New World") : worldName.trimmed();
    const QcResult<QcWorldLibrary::Entry> entry = library.create(name, seedFromText(seedText), keepInventory);
    if (!entry) {
        setPhase(u"error"_s, tr("Cannot create the world: %1").arg(entry.error()));
        return;
    }

    m_library->refreshWorlds();
    startWorld(entry->directory, entry->info.name, entry->info.seed);
}

void QcGameController::playWorld(const QString &folder)
{
    const QcWorldLibrary library(QcWorldLibrary::defaultRoot());
    const QList<QcWorldLibrary::Entry> entries = library.list();
    const auto it = std::ranges::find(entries, folder, &QcWorldLibrary::Entry::folder);
    if (it == entries.end() || !it->error.isEmpty()) {
        setPhase(u"error"_s, it == entries.end() ? tr("World not found") : tr("Cannot load the world: %1").arg(it->error));
        return;
    }

    startWorld(it->directory, it->info.name, it->info.seed);
}

void QcGameController::saveWorld() const
{
    sendChat(u"/save"_s);
}

void QcGameController::sendChat(const QString &text) const
{
    if (m_parts.session) {
        m_parts.session->sendChat(text);
    }
}

void QcGameController::setOverlay(const QString &overlay)
{
    if (m_overlay == overlay) {
        return;
    }

    m_overlay = overlay;
    m_input->clearKeys();
    if (m_overlay != u"chat"_s) {
        m_chatStart.clear();
    }
    if (m_parts.session) {
        m_parts.session->setMining(false);
        // Leaving the inventory hands back the grid contents (and closes a crafting table); leaving
        // the furnace or chest screen closes the block.
        if (m_overlay != u"inventory"_s) {
            m_parts.session->closeCrafting();
        }
        if (m_overlay != u"furnace"_s && m_overlay != u"chest"_s) {
            m_parts.session->closeMachine();
        }
    }
    emit overlayChanged();
    // Opening a screen frees the mouse; closing the last one captures it again without a click.
    m_input->syncCapture();
}

void QcGameController::openChat(const QString &start)
{
    m_chatStart = start;
    setOverlay(u"chat"_s);
}

void QcGameController::startWorld(const QString &directory, const QString &worldName, qint64 seed)
{
    if (const QcStatus saved = stopSession(); !saved) {
        qCWarning(lcGame) << "Saving the previous world failed:" << saved.error();
    }
    m_worldDirectory = directory;
    setOverlay({});
    setPhase(u"loading"_s, tr("Loading content..."));

    const std::shared_ptr<const QcContentRegistry> content = loadContent();
    if (!content || !startServer(content, worldName, seed)) {
        return;
    }

    createSession();
    connectSessionWorld();
    connectSessionPlayer();
    if (m_parts.view) {
        m_parts.view->clearMeshes();
        const int viewDistance = m_settings->viewDistance();
        m_parts.view->setFogRange(float(viewDistance * QcWorld::chunkSize) * 0.6f, float(viewDistance * QcWorld::chunkSize));
    }

    m_sinceTick.start();
    m_parts.session->start();
    setPhase(u"loading"_s, tr("Generating world..."));
    emit sessionStarted(m_parts.session);
}

std::shared_ptr<const QcContentRegistry> QcGameController::loadContent()
{
    QStringList warnings;
    QcResult<QcModLoader::Result> content = QcModLoader::load(m_library->modsDirectory(), warnings);
    for (const QString &warning : std::as_const(warnings)) {
        qCWarning(lcGame) << "Content:" << warning;
    }
    if (!content) {
        setPhase(u"error"_s, tr("Cannot load game content: %1").arg(content.error()));
        return nullptr;
    }

    m_library->recordLoadedMods(content->mods);
    const std::shared_ptr<const QcContentRegistry> shared = std::move(content->content);
    m_library->useContent(shared);
    m_settings->character()->refreshLists();
    return shared;
}

bool QcGameController::startServer(const std::shared_ptr<const QcContentRegistry> &content, const QString &worldName, qint64 seed)
{
    QcServerConfig config;
    config.worldName = worldName;
    config.seed = seed;
    config.worldDirectory = m_worldDirectory;
    // One extra ring: sections are meshed only once their neighbours are known.
    config.viewDistance = m_settings->viewDistance() + 1;
    config.spawnColumn = m_startup->spawnColumn();
    QStringList generatorWarnings;
    auto generator = std::make_unique<QcTerrainGenerator>(content, config.seed, &generatorWarnings);
    for (const QString &warning : std::as_const(generatorWarnings)) {
        qCWarning(lcGame) << "World generation:" << warning;
    }

    m_parts.server = std::make_unique<QcLocalServerHost>(config, content, std::move(generator));
    if (!m_parts.server->startError().isEmpty()) {
        const QString error = m_parts.server->worldInUse()
                                  ? tr("It is already open in another QtCraft window or process. Close it there first.")
                                  : m_parts.server->startError();
        m_parts.server.reset();
        setPhase(u"error"_s, tr("Cannot load the world: %1").arg(error));
        return false;
    }
    return true;
}

void QcGameController::createSession()
{
    m_parts.session = new QcClientSession(m_parts.server->connectClient(), u"Player"_s, this);
    m_parts.session->setAppearance(m_settings->character()->appearance());
    m_parts.session->setCameraMode(m_settings->cameraModeValue());
    m_parts.meshes = new QcMeshScheduler(this);
    m_parts.session->setIntent(m_startup->initialIntent());
    m_render.setForcedPose(m_startup->forcedPose());
}

void QcGameController::connectSessionWorld()
{
    const QcClientSession *session = m_parts.session;
    connect(session, &QcClientSession::contentReady, this, &QcGameController::onContentReady);
    connect(session, &QcClientSession::chunkLoaded, this, [this](QcChunkPos pos) { m_parts.meshes->chunkLoaded(*m_parts.session->world(), pos); });
    connect(session, &QcClientSession::chunkUnloaded, m_parts.meshes, &QcMeshScheduler::chunkUnloaded);
    connect(session, &QcClientSession::blockChanged, this, [this](QcBlockPos pos, QcBlockId previous) {
        m_parts.meshes->blockChanged(*m_parts.session->world(), pos, previous);
    });
    connect(session, &QcClientSession::ticked, this, [this] {
        m_sinceTick.restart();
        m_audio->setAmbience(m_parts.session->ambience());
    });
    connect(session, &QcClientSession::soundCue, this, [this](const QcSoundCue &cue) {
        const QcVec3 ears = m_parts.session->playerMotion().position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
        // Left and right as seen on screen: the front view looks back at the player.
        const bool front = m_parts.session->cameraMode() == QcCameraMode::Front;
        m_audio->play(cue, ears, m_parts.session->intent().yaw + (front ? 180.0f : 0.0f));
    });
    connect(session, &QcClientSession::chatReceived, m_hud, &QcHudState::addChatLine);
    connect(session, &QcClientSession::stateChanged, this, &QcGameController::onSessionState);

    connect(m_parts.meshes, &QcMeshScheduler::meshReady, this, [this](quint64 key, std::shared_ptr<const QcChunkMesh> mesh) {
        if (m_parts.view) {
            m_parts.view->submitMesh(key, std::move(mesh));
        }
    });
    connect(m_parts.meshes, &QcMeshScheduler::meshRemoved, this, [this](quint64 key) {
        if (m_parts.view) {
            m_parts.view->removeMesh(key);
        }
    });
}

void QcGameController::connectSessionPlayer()
{
    const QcClientSession *session = m_parts.session;
    connect(session, &QcClientSession::inventoryChanged, m_player, &QcPlayerController::updateInventory);
    connect(session, &QcClientSession::craftingOpened, this, [this] { setOverlay(u"inventory"_s); });
    connect(session, &QcClientSession::foodChanged, m_player, &QcPlayerController::foodChanged);
    connect(session, &QcClientSession::machineOpened, this, &QcGameController::onMachineOpened);
    connect(session, &QcClientSession::machineChanged, m_player, &QcPlayerController::updateMachine);
    connect(session, &QcClientSession::machineClosed, this, &QcGameController::onMachineClosed);
    // On death the screens close before the health properties notify QML.
    connect(session, &QcClientSession::healthChanged, this, &QcGameController::onHealthChanged);
    connect(session, &QcClientSession::healthChanged, m_player, &QcPlayerController::healthChanged);
}

void QcGameController::onSessionState(QcClientSession::State state)
{
    if (state == QcClientSession::State::Playing) {
        setPhase(u"playing"_s);
        m_startup->worldPlaying();
    } else if (state == QcClientSession::State::Disconnected && m_phase != u"menu"_s) {
        m_input->releaseMouse();
        setPhase(u"error"_s, tr("Disconnected: %1").arg(m_parts.session->disconnectReason()));
    }
}

void QcGameController::onMachineOpened()
{
    m_player->nameOpenMachine();
    setOverlay(m_parts.session->containerOpen() ? u"chest"_s : u"furnace"_s);
    m_player->updateMachine();
    m_startup->machineOpened();
}

void QcGameController::onMachineClosed()
{
    if (m_overlay == u"furnace"_s || m_overlay == u"chest"_s) {
        setOverlay({});
    }
    m_player->updateMachine();
}

void QcGameController::onHealthChanged()
{
    if (!m_parts.session->isDead()) {
        // Respawned: back into the world with the mouse captured.
        m_input->syncCapture();
        return;
    }

    setOverlay({});
    m_input->releaseMouse();
    // Releases on the death screen go to QML; forget held keys so nothing sticks after respawn.
    m_input->clearKeys();
    m_parts.session->setMining(false);
}

void QcGameController::onContentReady()
{
    const QcContentRegistry &content = *m_parts.session->content();
    const QcTextureAtlas atlas = QcTextureAtlas::build(content);
    const QStringList problems = atlas.problems();
    for (const QString &problem : problems) {
        qCWarning(lcGame) << "Texture:" << problem;
    }

    m_renderTable = std::make_shared<const QcBlockRenderTable>(content, atlas);
    m_render.setAtlas(std::make_shared<const QcTextureAtlas>(atlas));
    m_icons.draw(content, atlas, m_renderTable.get());
    m_player->useContent();
    m_parts.meshes->setRenderTable(m_renderTable);
    if (m_parts.view) {
        m_parts.view->setAtlas(atlas.image());
    }
    m_audio->setContent(m_parts.session->content());
    m_audio->setMusicEnabled(true);
}

void QcGameController::onContentReset()
{
    if (!m_parts.session) {
        m_icons.invalidate();
    }
    m_settings->character()->refreshLists();
}

QcStatus QcGameController::stopSession()
{
    QcStatus saved;
    m_input->releaseMouse();
    m_audio->setMusicEnabled(false);
    m_audio->stopWorldSounds();
    if (m_parts.meshes) {
        m_parts.meshes->waitForJobs();
        delete m_parts.meshes;
        m_parts.meshes = nullptr;
    }
    if (m_parts.session) {
        m_parts.session->disconnectFromServer();
        delete m_parts.session;
        m_parts.session = nullptr;
    }
    if (m_parts.server) {
        saved = m_parts.server->shutdown();
        m_parts.server.reset();
    }
    if (m_parts.view) {
        m_parts.view->clearMeshes();
    }

    // Back in the menu: the menu's own content instead of the last world's sounds.
    if (m_audio->isActive() && m_library->loadedContent()) {
        m_audio->setContent(m_library->loadedContent());
    }
    m_input->clearKeys();
    return saved;
}

void QcGameController::leaveGame()
{
    if (const QcStatus saved = stopSession(); !saved) {
        setPhase(u"error"_s, tr("The world could not be saved: %1").arg(saved.error()));
        return;
    }
    setPhase(u"menu"_s);
}

float QcGameController::advanceFrameClocks()
{
    const float frameSeconds = float(std::min<qint64>(m_frameClock.restart(), 100)) / 1000.0f;
    ++m_framesSinceFps;
    if (m_fpsClock.elapsed() >= 500) {
        m_fps = m_framesSinceFps * 1000.0 / double(m_fpsClock.restart());
        m_framesSinceFps = 0;
    }
    return frameSeconds;
}

void QcGameController::onFrame()
{
    m_benchmark->recordFrame();
    const float frameSeconds = advanceFrameClocks();
    const QPointF mouseDelta = m_input->takeMouseDelta();
    if (!m_parts.session || !m_parts.view || !m_parts.session->world()) {
        return;
    }

    // The world moves every frame (interpolated motion, animations, day cycle): the next frame is
    // requested explicitly, not left to item changes made during this one.
    if (m_parts.window) {
        m_parts.window->update();
    }
    m_input->applyFrame(mouseDelta, frameSeconds);

    const double alpha = std::clamp(double(m_sinceTick.elapsed()) / (QcPhysics::tickDuration * 1000.0), 0.0, 1.0);
    const QcVec3 feet = lerp(m_parts.session->previousPlayerMotion().position, m_parts.session->playerMotion().position, alpha);
    const QcVec3 eye = feet + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
    // The control interface may switch the view too; the session holds the current mode.
    const QcCameraMode mode = m_parts.session->isDead() ? QcCameraMode::FirstPerson : m_parts.session->cameraMode();
    m_render.placeCamera(eye, mode);

    QElapsedTimer entityTime;
    entityTime.start();
    m_render.updateEntities(alpha, mode);
    m_render.updateHand(alpha, eye, mode);
    m_benchmark->recordGuiFrame(entityTime.nsecsElapsed());
    m_render.updateSky(alpha);
    m_hud->updateTimeOfDay(alpha);
    m_render.updateSelection();
    m_hud->updateTarget();

    m_parts.meshes->setFocus({feet.x, feet.y, feet.z});
    m_parts.meshes->pump(*m_parts.session->world());

    m_hud->updateDebugText(feet, m_fps);
    m_startup->releaseScreenshotWhenLoaded(m_settings->viewDistance(), m_phase == u"playing"_s);
}
