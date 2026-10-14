#include "QcGameController.h"

#include "QcAudioEngine.h"
#include "QcBenchmarkRun.h"
#include "QcBlockRenderTable.h"
#include "QcDebugJson.h"
#include "QcLocalServerHost.h"
#include "QcMeshScheduler.h"
#include "QcModLoader.h"
#include "QcPhysics.h"
#include "QcPlayerList.h"
#include "QcStatsLog.h"
#include "QcWorldGenerators.h"
#include "QcTextureAtlas.h"
#include "QcWorldLibrary.h"

#include <QCoreApplication>
#include <QDir>
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
    , m_preview(new QcPreviewSource(m_parts, *m_library, *m_settings->character(), *m_settings->texturePacks(), this))
    , m_benchmark(new QcBenchmarkRun(*this, m_parts, *m_input, *m_settings, this))
    , m_startup(new QcStartupHooks(*this, m_parts, m_benchmark, this))
    , m_multiplayer(new QcMultiplayerController(*this, m_parts, *m_settings, *m_library, this))
    , m_render(m_parts, *m_settings)
{
    m_frameClock.start();
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
    // Texture packs switched in the settings (decision E-301): the world gets the new atlas, the
    // menu draws its icons and previews again.
    connect(m_settings->texturePacks(), &QcTexturePackController::atlasBuilt, this, &QcGameController::applyAtlas);
    connect(m_settings->texturePacks(), &QcTexturePackController::stackChanged, this, [this] {
        if (!m_parts.session) {
            m_icons.invalidate();
            m_preview->useMenu();
        }
    });
    // Players joining and leaving appear in the chat (decision E-259).
    connect(m_multiplayer->players(), &QcPlayerList::announced, m_hud, &QcHudState::addChatLine);
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

void QcGameController::startSingleplayer(const QString &worldName, const QString &seedText, const QString &generator,
                                         const QString &options, const QString &gameMode)
{
    QcWorldInfo world;
    world.name = worldName.trimmed().isEmpty() ? tr("New World") : worldName.trimmed();
    world.seed = seedFromText(seedText);
    world.generator = generator;
    world.generatorOptions = options.trimmed();
    world.defaultGameMode = QcGameModes::parse(gameMode).value_or(QcGameMode::Survival);
    startWorld({}, world);
}

void QcGameController::createWorld(const QString &worldName, const QString &seedText, const QVariantMap &setup)
{
    QcWorldLibrary::NewWorld world;
    world.keepInventory = setup.value(u"keepInventory"_s, true).toBool();
    world.generator = setup.value(u"worldType"_s, world.generator).toString();
    world.gameMode = QcGameModes::parse(setup.value(u"gameMode"_s).toString()).value_or(QcGameMode::Survival);

    // Wrong layers of a flat world are refused before anything is created; the world keeps the
    // defaults of its type as they are now.
    const QcResult<QString> stored =
        QcWorldGenerators::optionsToStore(world.generator, setup.value(u"layers"_s).toString().trimmed(), m_library->content());
    if (!stored) {
        setPhase(u"error"_s, tr("Cannot create the world: %1").arg(stored.error()));
        return;
    }

    const QcWorldLibrary library(QcWorldLibrary::defaultRoot());
    const QString name = worldName.trimmed().isEmpty() ? tr("New World") : worldName.trimmed();
    world.options = *stored;
    const QcResult<QcWorldLibrary::Entry> entry = library.create(name, seedFromText(seedText), world);
    if (!entry) {
        setPhase(u"error"_s, tr("Cannot create the world: %1").arg(entry.error()));
        return;
    }

    m_library->refreshWorlds();
    startWorld(entry->directory, entry->info);
}

QString QcGameController::defaultFlatLayers()
{
    return m_library->content().flatWorld().preset;
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

    startWorld(it->directory, it->info);
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

void QcGameController::startWorld(const QString &directory, const QcWorldInfo &world)
{
    if (const QcStatus saved = stopSession(); !saved) {
        qCWarning(lcGame) << "Saving the previous world failed:" << saved.error();
    }
    m_worldDirectory = directory;
    setOverlay({});
    setPhase(u"loading"_s, tr("Loading content..."));

    const std::shared_ptr<const QcContentRegistry> content = loadContent();
    if (!content || !startServer(content, world)) {
        return;
    }

    m_parts.session = new QcClientSession(m_parts.server->connectClient(), u"Player"_s, this);
    m_parts.session->setAppearance(m_settings->character()->appearance());
    beginSession();
    m_parts.session->start();
    setPhase(u"loading"_s, tr("Generating world..."));
    emit sessionStarted(m_parts.session);
}

void QcGameController::startRemoteSession(QcClientSession *session)
{
    if (const QcStatus saved = stopSession(); !saved) {
        qCWarning(lcGame) << "Saving the previous world failed:" << saved.error();
    }
    // A network game: the server keeps the world, nothing is saved here.
    m_worldDirectory.clear();
    setOverlay({});

    session->setParent(this);
    m_parts.session = session;
    beginSession();
    setPhase(u"loading"_s, tr("Loading the world..."));
    emit sessionStarted(m_parts.session);
}

void QcGameController::beginSession()
{
    m_parts.session->setCameraMode(m_settings->cameraModeValue());
    m_parts.meshes = new QcMeshScheduler(this);
    m_parts.session->setIntent(m_startup->initialIntent());
    m_render.setForcedPose(m_startup->forcedPose());
    m_hud->debug()->syncSubscription();
    connectSessionWorld();
    connectSessionPlayer();
    if (m_parts.view) {
        m_parts.view->clearMeshes();
        const int viewDistance = m_settings->viewDistance();
        m_parts.view->setFogRange(float(viewDistance * QcWorld::chunkSize) * 0.6f, float(viewDistance * QcWorld::chunkSize));
    }
    m_sinceTick.start();
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
    m_loadedMods = content->mods;
    const std::shared_ptr<const QcContentRegistry> shared = std::move(content->content);
    m_library->useContent(shared);
    m_settings->character()->refreshLists();
    return shared;
}

QString QcGameController::damagedFileMessage(const QcServerPersistence::DamagedFile &damaged)
{
    using Part = QcServerPersistence::DamagedPart;
    switch (damaged.part) {
    case Part::Entities:
        return tr("The file %1 of this world is damaged. Moved out of the world folder, it lets the world open without the creatures, "
                  "items and armour stands saved in it.")
            .arg(damaged.file);
    case Part::BlockEntities:
        return tr("The file %1 of this world is damaged. Moved out of the world folder, it lets the world open without the contents "
                  "of the chests and machines saved in it.")
            .arg(damaged.file);
    case Part::None:
        break;
    }
    return {};
}

bool QcGameController::startServer(const std::shared_ptr<const QcContentRegistry> &content, const QcWorldInfo &world)
{
    QcServerConfig config;
    config.worldName = world.name;
    config.seed = world.seed;
    config.generator = world.generator;
    config.generatorOptions = world.generatorOptions;
    config.worldDirectory = m_worldDirectory;
    // One extra ring: sections are meshed only once their neighbours are known.
    config.viewDistance = m_settings->viewDistance() + 1;
    config.spawnColumn = m_startup->spawnColumn();
    // An unsaved world (test hooks) has no world.json to keep its default game mode.
    if (m_worldDirectory.isEmpty()) {
        config.defaultGameMode = world.defaultGameMode;
    }
    // Options given for an unsaved world (--flat-layers) must fit; a saved world with damaged ones
    // falls back to the defaults with a warning.
    if (const QcStatus checked = QcWorldGenerators::checkOptions(world.generator, world.generatorOptions, *content);
        m_worldDirectory.isEmpty() && !checked) {
        setPhase(u"error"_s, tr("Cannot start the world: %1").arg(checked.error()));
        emit worldRefused(checked.error());
        return false;
    }

    QStringList generatorWarnings;
    std::unique_ptr<QcWorldGenerator> generator =
        QcWorldGenerators::create(world.generator, content, config.seed, &generatorWarnings, world.generatorOptions);
    for (const QString &warning : std::as_const(generatorWarnings)) {
        qCWarning(lcGame) << "World generation:" << warning;
    }
    if (!generator) {
        setPhase(u"error"_s, tr("Cannot load the world: it was made by the unknown world type '%1'").arg(world.generator));
        return false;
    }

    m_parts.server = std::make_unique<QcLocalServerHost>(config, content, std::move(generator));
    if (!m_parts.server->startError().isEmpty()) {
        QString error = m_parts.server->startError();
        if (m_parts.server->worldInUse()) {
            error = tr("It is already open in another QtCraft window or process. Close it there first.");
        } else if (m_parts.server->worldTooOld()) {
            error = tr("It was saved by an older development version of QtCraft and cannot be opened.");
        } else if (const QString damaged = damagedFileMessage(m_parts.server->damagedFile()); !damaged.isEmpty()) {
            error = damaged;
            m_library->noteUnplayable(QDir(m_worldDirectory).dirName(), damaged);
        }
        m_parts.server.reset();
        setPhase(u"error"_s, tr("Cannot load the world: %1").arg(error));
        return false;
    }
    if (!m_worldDirectory.isEmpty()) {
        m_library->noteUnplayable(QDir(m_worldDirectory).dirName(), {});
    }
    return true;
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
        m_hud->debug()->meshed(key);
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
    connect(session, &QcClientSession::gameModeChanged, m_player, &QcPlayerController::gameModeChanged);
    // An operator changed the mode while the creative inventory was open: the usual one instead.
    connect(session, &QcClientSession::gameModeChanged, this, [this] {
        if (m_overlay == u"creative"_s && !m_player->creative()) {
            setOverlay(u"inventory"_s);
        }
    });
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
        // The disconnect screen shows the reason (translated, E-094) and leads back to the menu.
        m_input->releaseMouse();
        setPhase(u"disconnected"_s, m_parts.session->disconnectReason());
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
    const std::shared_ptr<const QcTextureAtlas> atlas = m_settings->texturePacks()->enterWorld(m_parts.session->content());
    const QStringList problems = atlas->problems();
    for (const QString &problem : problems) {
        qCWarning(lcGame) << "Texture:" << problem;
    }

    m_atlas.reset();
    applyAtlas(atlas);
    m_player->useContent();
    m_audio->setContent(m_parts.session->content());
    m_audio->setMusicEnabled(true);
}

void QcGameController::applyAtlas(const std::shared_ptr<const QcTextureAtlas> &atlas)
{
    if (!m_parts.session || !m_parts.session->content() || !atlas) {
        return;
    }

    // The time on the GUI thread is logged for the pack switch benchmark (decision E-332); the
    // upload follows on the render thread.
    QElapsedTimer timer;
    timer.start();
    const QcContentRegistry &content = *m_parts.session->content();
    // The UVs depend only on the list of textures (E-300): a new pack only needs the new image, the
    // meshes stay; a different layout (other content) needs a new render table and new meshes.
    if (!m_atlas || !m_renderTable || !atlas->sameLayout(*m_atlas)) {
        m_renderTable = std::make_shared<const QcBlockRenderTable>(content, *atlas);
        m_parts.meshes->setRenderTable(m_renderTable);
    }
    m_atlas = atlas;
    m_render.setAtlas(atlas);
    m_preview->useWorld(m_parts.session->content(), atlas);
    const qint64 iconsStart = timer.nsecsElapsed();
    m_icons.draw(content, *atlas, m_renderTable.get());
    const qint64 iconsNs = timer.nsecsElapsed() - iconsStart;
    if (m_parts.view) {
        m_parts.view->setAtlas(atlas->levels());
    }
    const qint64 totalNs = timer.nsecsElapsed();
    qCInfo(lcGame).noquote() << u"Texture atlas applied in %1 ms (item icons %2 ms)"_s.arg(double(totalNs) / 1e6, 0, 'f', 1)
                                    .arg(double(iconsNs) / 1e6, 0, 'f', 1);
}

void QcGameController::onContentReset()
{
    if (!m_parts.session) {
        m_icons.invalidate();
        m_preview->useMenu();
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
        m_parts.view->setAtlas({});
    }
    m_hud->debug()->clearWorld();
    m_preview->useMenu();
    // The world's atlas and content go (decision E-301); a build for it still running is dropped.
    m_settings->texturePacks()->leaveWorld();
    m_atlas.reset();
    m_renderTable.reset();
    m_render.setAtlas(nullptr);

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
    return float(std::min<qint64>(m_frameClock.restart(), 100)) / 1000.0f;
}

void QcGameController::startStatsLog(int intervalMs)
{
    if (intervalMs <= 0 || m_statsLog) {
        return;
    }

    m_hud->debug()->setStatsLogging(true);
    m_statsLog = new QcStatsLog(intervalMs, [this]() -> std::optional<QJsonObject> {
        if (!m_parts.session || m_parts.session->state() != QcClientSession::State::Playing) {
            return std::nullopt;
        }
        return QcDebugJson::toJson(m_hud->debug()->snapshot(m_drawnFeet, QcFrameMonitor::Reader::StatsLog));
    }, this);
}

void QcGameController::onFrame()
{
    m_benchmark->recordFrame();
    const float frameSeconds = advanceFrameClocks();
    const QPointF mouseDelta = m_input->takeMouseDelta();
    // Taken every frame, also in the menu, so it never adds up across frames.
    const qint64 previewNs = m_preview->takeBuildNs();
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
    m_render.placeCamera(eye, mode, frameSeconds);

    QElapsedTimer entityTime;
    entityTime.start();
    m_render.updateEntities(alpha, mode);
    m_render.updateHand(alpha, eye, mode);
    const qint64 entityNs = entityTime.nsecsElapsed();
    const QList<QcRenderSample> renderSamples = m_hud->debug()->recordFrame(entityNs, previewNs, m_benchmark->isRunning());
    m_benchmark->recordGuiFrame(m_hud->debug()->lastGuiCpuMs(), entityNs, previewNs, renderSamples);
    m_render.updateSky(alpha);
    m_hud->updateTimeOfDay(alpha);
    m_render.updateSelection();
    m_hud->updateTarget();
    m_hud->updateLabels();

    m_parts.meshes->setFocus({feet.x, feet.y, feet.z});
    m_parts.meshes->pump(*m_parts.session->world());

    m_drawnFeet = feet;
    m_hud->debug()->update(feet);
    m_hud->debug()->updateWorld(alpha, feet);
    m_startup->releaseScreenshotWhenLoaded(m_settings->viewDistance(), m_phase == u"playing"_s);
}
