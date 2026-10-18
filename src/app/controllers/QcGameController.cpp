#include "app/controllers/QcGameController.h"

#include "app/hooks/QcBenchmarkRun.h"
#include "app/multiplayer/QcPlayerList.h"
#include "app/rendering/QcFramePlan.h"
#include "audio/QcAudioEngine.h"
#include "clientcore/meshing/QcMeshScheduler.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "server/core/QcLocalServerHost.h"
#include "server/storage/QcWorldLibrary.h"
#include "worldgen/generators/QcWorldGenerators.h"

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

} // namespace

QcGameController::QcGameController(QObject *parent)
    : QObject(parent)
    , m_audio(std::make_unique<QcAudioEngine>())
    , m_library(std::make_unique<QcWorldLibraryController>())
    , m_settings(std::make_unique<QcSettingsController>(m_parts, *m_library, m_audio.get()))
    , m_worlds(*m_library, *m_settings)
    , m_player(std::make_unique<QcPlayerController>(m_parts, *m_settings))
    , m_trade(std::make_unique<QcTradeController>(m_parts, *m_settings))
    , m_hud(std::make_unique<QcHudState>(m_parts, *m_settings))
    , m_handbook(std::make_unique<QcHandbookController>(m_parts, *m_settings, *m_library, m_icons))
    , m_input(std::make_unique<QcGameInput>(*this, m_parts, *m_settings, m_audio.get()))
    , m_preview(std::make_unique<QcPreviewSource>(m_parts, *m_library, *m_settings->character(), *m_settings->texturePacks()))
    , m_benchmark(std::make_unique<QcBenchmarkRun>(*this, m_parts, *m_input, *m_settings))
    , m_startup(std::make_unique<QcStartupHooks>(*this, m_parts, m_benchmark.get()))
    , m_multiplayer(std::make_unique<QcMultiplayerController>(*this, m_parts, *m_settings, *m_library))
    , m_render(m_parts, *m_settings)
    , m_worldAtlas(m_parts, {*m_settings->texturePacks(), m_icons, *m_preview, m_render})
    , m_wiring(m_parts, {*m_audio, *m_hud, *m_player, *m_trade, *m_input, *m_startup, m_worldAtlas},
               {[this] { return m_overlay; }, [this](const QString &overlay) { setOverlay(overlay); }})
    , m_frame(m_parts, {*m_settings, *m_input, *m_preview, m_render, *m_hud, *m_benchmark, *m_startup}, [this] { return m_phase == Phase::Playing; })
{
    connect(m_handbook.get(), &QcHandbookController::opening, this, [this] {
        m_input->clearKeys();
        if (m_parts.session) {
            m_parts.session->setMining(false);
            m_parts.session->cancelUse();
        }
        m_input->releaseMouse();
    });
    // Closing the handbook gives the world its mouse back without a click.
    connect(m_handbook.get(), &QcHandbookController::openChanged, m_input.get(), &QcGameInput::syncCapture);
    connect(m_library.get(), &QcWorldLibraryController::contentReset, this, &QcGameController::onContentReset);
    // Texture packs switched in the settings (decision E-301): the world gets the new atlas, the
    // menu draws its icons and previews again.
    connect(m_settings->texturePacks(), &QcTexturePackController::atlasBuilt, this,
            [this](const std::shared_ptr<const QcTextureAtlas> &atlas) { m_worldAtlas.apply(atlas); });
    connect(m_settings->texturePacks(), &QcTexturePackController::stackChanged, this, [this] {
        if (!m_parts.session) {
            m_icons.invalidate();
            m_preview->useMenu();
        }
    });
    // Players joining and leaving appear in the chat (decision E-259).
    connect(m_multiplayer->players(), &QcPlayerList::announced, m_hud.get(), &QcHudState::addChatLine);
    connect(&m_wiring, &QcSessionWiring::playing, this, [this] { setPhase(Phase::Playing); });
    connect(&m_wiring, &QcSessionWiring::disconnected, this, &QcGameController::onSessionDisconnected);
    connect(&m_wiring, &QcSessionWiring::ticked, this, [this] { m_frame.restartTickClock(); });
}

QcGameController::~QcGameController()
{
    // The application-wide input filter reads the other parts; it stops before any of them dies.
    if (QCoreApplication *application = QCoreApplication::instance()) {
        application->removeEventFilter(m_input.get());
    }

    if (const QcStatus saved = stopSession(); !saved) {
        qCWarning(lcGame) << "Saving the world failed:" << saved.error();
    }
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
        connect(m_parts.window, &QQuickWindow::afterAnimating, this, [this] { m_frame.onFrame(); });
        connect(m_parts.window, &QWindow::activeChanged, this, [this] {
            if (!m_parts.window->isActive()) {
                m_input->clearKeys();
                m_input->releaseMouse();
            }
        });
    }
}

void QcGameController::setPhase(Phase phase, const QString &status)
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
        setPhase(Phase::Error, tr("Cannot create the world: %1").arg(stored.error()));
        return;
    }

    const QcWorldLibrary library(QcWorldLibrary::defaultRoot());
    const QString name = worldName.trimmed().isEmpty() ? tr("New World") : worldName.trimmed();
    world.options = *stored;
    const QcResult<QcWorldLibrary::Entry> entry = library.create(name, seedFromText(seedText), world);
    if (!entry) {
        setPhase(Phase::Error, tr("Cannot create the world: %1").arg(entry.error()));
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
        setPhase(Phase::Error, it == entries.end() ? tr("World not found") : tr("Cannot load the world: %1").arg(it->error));
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
        // A screen opening or closing lets a drawn bow go without a shot.
        m_parts.session->cancelUse();
        // Leaving the inventory hands back the grid contents (and closes a crafting table); leaving
        // the furnace or chest screen closes the block.
        if (m_overlay != u"inventory"_s) {
            m_parts.session->closeCrafting();
        }
        if (m_overlay != u"furnace"_s && m_overlay != u"chest"_s) {
            m_parts.session->closeMachine();
        }
        if (m_overlay != u"trade"_s) {
            m_parts.session->trading().close();
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
    setPhase(Phase::Loading, tr("Loading content..."));

    if (!startServer(world)) {
        return;
    }

    m_parts.session = std::make_unique<QcClientSession>(m_parts.server->connectClient(), u"Player"_s);
    m_parts.session->setAppearance(m_settings->character()->appearance());
    beginSession();
    m_parts.session->start();
    setPhase(Phase::Loading, tr("Generating world..."));
    emit sessionStarted(m_parts.session.get());
}

void QcGameController::startRemoteSession(std::unique_ptr<QcClientSession> session)
{
    if (const QcStatus saved = stopSession(); !saved) {
        qCWarning(lcGame) << "Saving the previous world failed:" << saved.error();
    }
    // A network game: the server keeps the world, nothing is saved here.
    m_worldDirectory.clear();
    setOverlay({});

    m_parts.session = std::move(session);
    beginSession();
    setPhase(Phase::Loading, tr("Loading the world..."));
    emit sessionStarted(m_parts.session.get());
}

void QcGameController::beginSession()
{
    m_parts.session->setCameraMode(m_settings->cameraModeValue());
    m_parts.meshes = std::make_unique<QcMeshScheduler>();
    // The meshes carry the light of their cells only for a frame plan that reads it.
    m_parts.meshes->setSectionLight(QcFramePlan::selected().sectionLight);
    m_parts.session->setIntent(m_startup->initialIntent());
    m_render.setForcedPose(m_startup->forcedPose());
    m_hud->debug()->syncSubscription();
    m_wiring.connectSession();
    if (m_parts.view) {
        m_parts.view->clearMeshes();
        const int viewDistance = m_settings->viewDistance();
        m_parts.view->setFogRange(float(viewDistance * QcWorld::chunkSize) * 0.6f, float(viewDistance * QcWorld::chunkSize));
    }
    m_frame.startTickClock();
}

bool QcGameController::startServer(const QcWorldInfo &world)
{
    const QcResult<std::shared_ptr<const QcContentRegistry>> content = m_worlds.loadContent();
    if (!content) {
        setPhase(Phase::Error, content.error());
        return false;
    }

    std::expected<std::unique_ptr<QcLocalServerHost>, QcWorldStarter::Refusal> server =
        m_worlds.startServer(*content, world, m_worldDirectory, m_startup->spawnColumn());
    if (!server) {
        const QcWorldStarter::Refusal &refusal = server.error();
        setPhase(Phase::Error, refusal.status);
        if (refusal.refusedOptions) {
            emit worldRefused(*refusal.refusedOptions);
        }
        return false;
    }

    m_parts.server = std::move(*server);
    return true;
}

void QcGameController::onSessionDisconnected(const QString &reason)
{
    if (m_phase != Phase::Menu) {
        // The disconnect screen shows the reason (translated, E-094) and leads back to the menu.
        m_input->releaseMouse();
        setPhase(Phase::Disconnected, reason);
    }
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
        m_parts.meshes.reset();
    }
    if (m_parts.session) {
        m_parts.session->disconnectFromServer();
        m_parts.session.reset();
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
    m_worldAtlas.leaveWorld();

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
        setPhase(Phase::Error, tr("The world could not be saved: %1").arg(saved.error()));
        return;
    }
    setPhase(Phase::Menu);
}

void QcGameController::startStatsLog(int intervalMs)
{
    m_frame.startStatsLog(intervalMs);
}
